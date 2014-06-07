/*-----------------------------------------------------------------------------
| Copyright (c) 2014, Nucleic Development Team.
|
| Distributed under the terms of the Modified BSD License.
|
| The full license is in the file COPYING.txt, distributed with this software.
|----------------------------------------------------------------------------*/
#include <Python.h>
#include "pythonhelpers.h"
#include "inttypes.h"
#include "atom.h"
#include "class_map.h"
#include "member.h"
#include "null_object.h"

#include "ignoredwarnings.h"


using namespace PythonHelpers;


static PyObject* class_map_str;


static PyObject* lookup_class_map( PyTypeObject* type )
{
    PyObject* py_map = PyDict_GetItem( type->tp_dict, class_map_str );
    if( py_map )
    {
        if( !ClassMap_Check( py_map ) )
        {
            return py_bad_internal_call( "class map has invalid type" );
        }
        return newref( py_map );
    }
    return py_bad_internal_call( "atom type has no class map" );
}


static PyObject*
Atom_new( PyTypeObject* type, PyObject* args, PyObject* kwargs )
{
    PyObjectPtr map_ptr( lookup_class_map( type ) );
    if( !map_ptr )
    {
        return 0;
    }
    PyObjectPtr self_ptr( PyType_GenericNew( type, args, kwargs ) );
    if( !self_ptr )
    {
        return 0;
    }
    Atom* atom = ( Atom* )self_ptr.get();
    ClassMap* map = ( ClassMap* )map_ptr.get();
    uint32_t count = ClassMap_MemberCount( map );
    if( count > 0 )
    {
        size_t memsize = sizeof( PyObject* ) * count;
        void* slotmem = PyObject_Malloc( memsize );
        if( !slotmem )
        {
            return PyErr_NoMemory();
        }
        memset( slotmem, 0, memsize );
        atom->slots = ( PyObject** )slotmem;
    }
    atom->class_map = ( ClassMap* )map_ptr.release();
    return self_ptr.release();
}


static int Atom_init( PyObject* self, PyObject* args, PyObject* kwargs )
{
    if( PyTuple_GET_SIZE( args ) > 0 )
    {
        py_type_fail( "__init__() takes no positional arguments" );
        return -1;
    }
    if( kwargs )
    {
        PyObject* key;
        PyObject* value;
        Py_ssize_t pos = 0;
        while( PyDict_Next( kwargs, &pos, &key, &value ) )
        {
            if( PyObject_SetAttr( self, key, value ) < 0 )
            {
                return -1;
            }
        }
    }
    return 0;
}


static void Atom_clear( Atom* self )
{
    uint32_t count = ClassMap_MemberCount( self->class_map );
    for( uint32_t i = 0; i < count; ++i )
    {
        Py_CLEAR( self->slots[i] );
    }
    Py_CLEAR( self->class_map );
}


static int Atom_traverse( Atom* self, visitproc visit, void* arg )
{
    uint32_t count = ClassMap_MemberCount( self->class_map );
    for( uint32_t i = 0; i < count; ++i )
    {
        Py_VISIT( self->slots[i] );
    }
    Py_VISIT( self->class_map );
    return 0;
}


static void Atom_dealloc( Atom* self )
{
    PyObject_GC_UnTrack( self );
    Atom_clear( self );
    PyObject_Free( self->slots );
    self->ob_type->tp_free( ( PyObject* )self );
}


static PyObject* do_validate( Member* member,
                              Atom* atom,
                              PyStringObject* name,
                              PyObject* old,
                              PyObject* val )
{
    PyObjectPtr result( newref( val ) );
    if( member->validate_handler )
    {
        PyObjectPtr args( PyTuple_New( 5 ) );
        if( !args )
        {
            return 0;
        }
        PyTuple_SET_ITEM( args.get(), 0, newref( ( PyObject* )member ) );
        PyTuple_SET_ITEM( args.get(), 1, newref( ( PyObject* )atom ) );
        PyTuple_SET_ITEM( args.get(), 2, newref( ( PyObject* )name ) );
        PyTuple_SET_ITEM( args.get(), 3, newref( ( PyObject* )old ) );
        PyTuple_SET_ITEM( args.get(), 4, newref( result.get() ) );
        result = PyObject_Call( member->validate_handler, args.get(), 0 );
        if( !result )
        {
            return 0;
        }
    }
    if( member->post_validate_handler )
    {
        PyObjectPtr args( PyTuple_New( 5 ) );
        if( !args )
        {
            return 0;
        }
        PyTuple_SET_ITEM( args.get(), 0, newref( ( PyObject* )member ) );
        PyTuple_SET_ITEM( args.get(), 1, newref( ( PyObject* )atom ) );
        PyTuple_SET_ITEM( args.get(), 2, newref( ( PyObject* )name ) );
        PyTuple_SET_ITEM( args.get(), 3, newref( ( PyObject* )old ) );
        PyTuple_SET_ITEM( args.get(), 4, newref( result.get() ) );
        result = PyObject_Call( member->post_validate_handler, args.get(), 0 );
        if( !result )
        {
            return 0;
        }
    }
    return result.release();
}


static PyObject* do_default( Member* member, Atom* atom, PyStringObject* name )
{
    PyObjectPtr result( newref( Py_None ) );
    if( member->default_handler )
    {
        PyObjectPtr args( PyTuple_New( 3 ) );
        if( !args )
        {
            return 0;
        }
        PyTuple_SET_ITEM( args.get(), 0, newref( ( PyObject* )member ) );
        PyTuple_SET_ITEM( args.get(), 1, newref( ( PyObject* )atom ) );
        PyTuple_SET_ITEM( args.get(), 2, newref( ( PyObject* )name ) );
        result = PyObject_Call( member->default_handler, args.get(), 0 );
        if( !result )
        {
            return 0;
        }
    }
    return do_validate( member, atom, name, NullObject, result.get() );
}


static int do_post_setattr( Member* member,
                            Atom* atom,
                            PyStringObject* name,
                            PyObject* old,
                            PyObject* val )
{
    if( member->post_setattr_handler )
    {
        PyObjectPtr args( PyTuple_New( 5 ) );
        if( !args )
        {
            return 0;
        }
        PyTuple_SET_ITEM( args.get(), 0, newref( ( PyObject* )member ) );
        PyTuple_SET_ITEM( args.get(), 1, newref( ( PyObject* )atom ) );
        PyTuple_SET_ITEM( args.get(), 2, newref( ( PyObject* )name ) );
        PyTuple_SET_ITEM( args.get(), 3, newref( ( PyObject* )old ) );
        PyTuple_SET_ITEM( args.get(), 4, newref( ( PyObject* )val ) );
        PyObjectPtr result(
            PyObject_Call( member->post_setattr_handler, args.get(), 0 ) );
        if( !result )
        {
            return -1;
        }
    }
    return 0;
}


static PyObject* Atom_getattro( Atom* self, PyStringObject* name )
{
    uint32_t index;
    Member* member = 0;
    ClassMap_LookupMember( self->class_map, name, &member, &index );
    if( member )
    {
        PyObject* value = self->slots[index];
        if( value )
        {
            return newref( value );
        }
        value = do_default( member, self, name );
        if( !value )
        {
            return 0;
        }
        self->slots[index] = value;
        return newref( value );
    }
    return PyObject_GenericGetAttr( ( PyObject* )self, ( PyObject* )name );
}


static int Atom_setattro( Atom* self, PyStringObject* name, PyObject* value )
{
    uint32_t index;
    Member* member = 0;
    ClassMap_LookupMember( self->class_map, name, &member, &index );
    if( member )
    {
        self->slots[index] = xnewref( value );
        return 0;
    }
    return PyObject_GenericSetAttr(
        ( PyObject* )self, ( PyObject* )name, value );
}


static PyObject* Atom_sizeof( Atom* self, PyObject* args )
{
    Py_ssize_t size = self->ob_type->tp_basicsize;
    size += sizeof( PyObject* ) * ClassMap_MemberCount( self->class_map );
    return PyInt_FromSsize_t( size );
}


static PyMethodDef Atom_methods[] = {
    {"__sizeof__",
     ( PyCFunction )Atom_sizeof,
     METH_NOARGS,
     "__sizeof__() -> size of object in memory, in bytes"},
    {0} // sentinel
};


PyTypeObject Atom_Type = {
    PyObject_HEAD_INIT( &PyType_Type ) /* header */
    0,                                 /* ob_size */
    "atom.catom.Atom",                 /* tp_name */
    sizeof( Atom ),                    /* tp_basicsize */
    0,                                 /* tp_itemsize */
    ( destructor )Atom_dealloc,        /* tp_dealloc */
    ( printfunc )0,                    /* tp_print */
    ( getattrfunc )0,                  /* tp_getattr */
    ( setattrfunc )0,                  /* tp_setattr */
    ( cmpfunc )0,                      /* tp_compare */
    ( reprfunc )0,                     /* tp_repr */
    ( PyNumberMethods* )0,             /* tp_as_number */
    ( PySequenceMethods* )0,           /* tp_as_sequence */
    ( PyMappingMethods* )0,            /* tp_as_mapping */
    ( hashfunc )0,                     /* tp_hash */
    ( ternaryfunc )0,                  /* tp_call */
    ( reprfunc )0,                     /* tp_str */
    ( getattrofunc )Atom_getattro,     /* tp_getattro */
    ( setattrofunc )Atom_setattro,     /* tp_setattro */
    ( PyBufferProcs* )0,               /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_HAVE_VERSION_TAG,     /* tp_flags */
    0,                                   /* Documentation string */
    ( traverseproc )Atom_traverse,       /* tp_traverse */
    ( inquiry )Atom_clear,               /* tp_clear */
    ( richcmpfunc )0,                    /* tp_richcompare */
    0,                                   /* tp_weaklistoffset */
    ( getiterfunc )0,                    /* tp_iter */
    ( iternextfunc )0,                   /* tp_iternext */
    ( struct PyMethodDef* )Atom_methods, /* tp_methods */
    ( struct PyMemberDef* )0,            /* tp_members */
    0,                                   /* tp_getset */
    0,                                   /* tp_base */
    0,                                   /* tp_dict */
    ( descrgetfunc )0,                   /* tp_descr_get */
    ( descrsetfunc )0,                   /* tp_descr_set */
    0,                                   /* tp_dictoffset */
    ( initproc )Atom_init,               /* tp_init */
    ( allocfunc )PyType_GenericAlloc,    /* tp_alloc */
    ( newfunc )Atom_new,                 /* tp_new */
    ( freefunc )PyObject_GC_Del,         /* tp_free */
    ( inquiry )0,                        /* tp_is_gc */
    0,                                   /* tp_bases */
    0,                                   /* tp_mro */
    0,                                   /* tp_cache */
    0,                                   /* tp_subclasses */
    0,                                   /* tp_weaklist */
    ( destructor )0                      /* tp_del */
};


int import_atom()
{
    class_map_str = PyString_FromString( "_[class map]" );
    if( !class_map_str )
    {
        return -1;
    }
    return PyType_Ready( &Atom_Type );
}