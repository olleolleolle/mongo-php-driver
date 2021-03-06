// db.c
/**
 *  Copyright 2009 10gen, Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */


#include <php.h>
#include <zend_exceptions.h>
#if ZEND_MODULE_API_NO >= 20060613
#include "ext/spl/spl_exceptions.h"
#endif

#include "db.h"
#include "php_mongo.h"
#include "collection.h"
#include "cursor.h"
#include "gridfs.h"
#include "mongo_types.h"

extern zend_class_entry *mongo_ce_Mongo,
  *mongo_ce_Collection,
  *mongo_ce_Cursor,
  *mongo_ce_GridFS,
  *mongo_ce_Id,
  *mongo_ce_Code,
  *mongo_ce_Exception;

extern int le_pconnection,
  le_connection;

extern zend_object_handlers mongo_default_handlers;

zend_class_entry *mongo_ce_DB = NULL;

PHP_METHOD(MongoDB, __construct) {
  zval *zlink;
  char *name;
  int name_len;
  mongo_db *db;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Os", &zlink, mongo_ce_Mongo, &name, &name_len) == FAILURE) {
    return;
  }

  if (name_len == 0 ||
      strchr(name, ' ') ||
      strchr(name, '.')) {
#   if ZEND_MODULE_API_NO >= 20060613
    zend_throw_exception(spl_ce_InvalidArgumentException, "MongoDB::__construct(): database names must be at least one character and cannot contain ' ' or  '.'", 0 TSRMLS_CC);
#   else
    zend_throw_exception(zend_exception_get_default(), "MongoDB::__construct(): database names must be at least one character and cannot contain ' ' or  '.'", 0 TSRMLS_CC);
#   endif
    return;
  }

  db = (mongo_db*)zend_object_store_get_object(getThis() TSRMLS_CC);

  db->link = zend_read_property(mongo_ce_Mongo, zlink, "connection", strlen("connection"), NOISY TSRMLS_CC);
  zval_add_ref(&db->link);

  MAKE_STD_ZVAL(db->name);
  ZVAL_STRING(db->name, name, 1);
}

PHP_METHOD(MongoDB, __toString) {
  mongo_db *db = (mongo_db*)zend_object_store_get_object(getThis() TSRMLS_CC);
  MONGO_CHECK_INITIALIZED_STRING(db->name, MongoDB);
  RETURN_ZVAL(db->name, 1, 0);
}

PHP_METHOD(MongoDB, selectCollection) {
  zval temp;
  zval *collection;
  mongo_db *db;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &collection) == FAILURE) {
    return;
  }

  db = (mongo_db*)zend_object_store_get_object(getThis() TSRMLS_CC);
  MONGO_CHECK_INITIALIZED(db->name, MongoDB);
 
  object_init_ex(return_value, mongo_ce_Collection);

  PUSH_PARAM(getThis()); PUSH_PARAM(collection); PUSH_PARAM((void*)2);
  PUSH_EO_PARAM();
  MONGO_METHOD(MongoCollection, __construct)(2, &temp, NULL, return_value, return_value_used TSRMLS_CC); 
  POP_EO_PARAM();
  POP_PARAM(); POP_PARAM(); POP_PARAM();
}

PHP_METHOD(MongoDB, getGridFS) {
  zval temp;
  zval *arg1 = 0, *arg2 = 0;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|zz", &arg1, &arg2) == FAILURE) {
    return;
  }

  object_init_ex(return_value, mongo_ce_GridFS);

  PUSH_PARAM(getThis());

  if (arg1) {
    PUSH_PARAM(arg1);
    if (arg2) {
      PUSH_PARAM(arg2);
    }
  }

  PUSH_PARAM((void*)(ZEND_NUM_ARGS()+1));
  PUSH_EO_PARAM();
  MONGO_METHOD(MongoGridFS, __construct)(ZEND_NUM_ARGS()+1, &temp, NULL, return_value, return_value_used TSRMLS_CC);
  POP_EO_PARAM();
  POP_PARAM(); POP_PARAM();

  if (arg1) {
    POP_PARAM();
    if (arg2) {
      POP_PARAM();
    }
  }
}

PHP_METHOD(MongoDB, getProfilingLevel) {
  zval l;
  Z_TYPE(l) = IS_LONG;
  Z_LVAL(l) = -1;

  PUSH_PARAM(&l); PUSH_PARAM((void*)1);
  PUSH_EO_PARAM();
  MONGO_METHOD(MongoDB, setProfilingLevel)(1, return_value, return_value_ptr, getThis(), return_value_used TSRMLS_CC);
  POP_EO_PARAM();
  POP_PARAM(); POP_PARAM();
}

PHP_METHOD(MongoDB, setProfilingLevel) {
  long level;
  zval *data, *cmd_return;
  zval **ok;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &level) == FAILURE) {
    return;
  }

  MAKE_STD_ZVAL(data);
  array_init(data);
  add_assoc_long(data, "profile", level);

  MAKE_STD_ZVAL(cmd_return);

  PUSH_PARAM(data); PUSH_PARAM((void*)1);
  PUSH_EO_PARAM();
  MONGO_METHOD(MongoDB, command)(1, cmd_return, &cmd_return, getThis(), return_value_used TSRMLS_CC);
  POP_EO_PARAM();
  POP_PARAM(); POP_PARAM();

  if (zend_hash_find(HASH_P(cmd_return), "ok", 3, (void**)&ok) == SUCCESS &&
      Z_DVAL_PP(ok) == 1) {
    zend_hash_find(HASH_P(cmd_return), "was", 4, (void**)&ok);
    RETVAL_ZVAL(*ok, 1, 0);
  }
  else {
    RETVAL_NULL();
  }
  zval_ptr_dtor(&data);
  zval_ptr_dtor(&cmd_return);
}

PHP_METHOD(MongoDB, drop) {
  zval *data;
  MAKE_STD_ZVAL(data);
  array_init(data);
  add_assoc_long(data, "dropDatabase", 1);

  PUSH_PARAM(data); PUSH_PARAM((void*)1);
  PUSH_EO_PARAM();
  MONGO_METHOD(MongoDB, command)(1, return_value, return_value_ptr, getThis(), return_value_used TSRMLS_CC);
  POP_EO_PARAM();
  POP_PARAM(); POP_PARAM();

  zval_ptr_dtor(&data);
}

PHP_METHOD(MongoDB, repair) {
  zend_bool cloned=0, original=0;
  zval *data;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|bb", &cloned, &original) == FAILURE) {
    return;
  }

  MAKE_STD_ZVAL(data);
  array_init(data);
  add_assoc_long(data, "repairDatabase", 1);
  add_assoc_bool(data, "preserveClonedFilesOnFailure", cloned);
  add_assoc_bool(data, "backupOriginalFiles", original);

  PUSH_PARAM(data); PUSH_PARAM((void*)1);
  PUSH_EO_PARAM();
  MONGO_METHOD(MongoDB, command)(1, return_value, return_value_ptr, getThis(), return_value_used TSRMLS_CC);
  POP_EO_PARAM();
  POP_PARAM(); POP_PARAM();

  zval_ptr_dtor(&data);
}


PHP_METHOD(MongoDB, createCollection) {
  zval *collection, *data, *temp;
  zend_bool capped=0;
  int size=0, max=0;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|bll", &collection, &capped, &size, &max) == FAILURE) {
    return;
  }

  MAKE_STD_ZVAL(data);
  array_init(data);
  convert_to_string(collection);
  add_assoc_zval(data, "create", collection);
  zval_add_ref(&collection);

  if (size) {
    add_assoc_long(data, "size", size);
  }

  if (capped) {
    add_assoc_bool(data, "capped", 1);
    if (max) {
      add_assoc_long(data, "max", max);
    }
  }

  MAKE_STD_ZVAL(temp);
  PUSH_PARAM(data); PUSH_PARAM((void*)1);
  PUSH_EO_PARAM();
  MONGO_METHOD(MongoDB, command)(1, temp, NULL, getThis(), return_value_used TSRMLS_CC);
  POP_EO_PARAM();
  POP_PARAM(); POP_PARAM();

  zval_ptr_dtor(&temp);
  zval_ptr_dtor(&data);

  // get the collection we just created
  PUSH_PARAM(collection); PUSH_PARAM((void*)1);
  PUSH_EO_PARAM();
  MONGO_METHOD(MongoDB, selectCollection)(1, return_value, return_value_ptr, getThis(), return_value_used TSRMLS_CC);
  POP_EO_PARAM();
  POP_PARAM(); POP_PARAM();
}

PHP_METHOD(MongoDB, dropCollection) {
  zval temp;
  zval *collection;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &collection) == FAILURE) {
    return;
  }

  if (Z_TYPE_P(collection) != IS_OBJECT ||
      Z_OBJCE_P(collection) != mongo_ce_Collection) {

    PUSH_PARAM(collection); PUSH_PARAM((void*)1);
    PUSH_EO_PARAM();
    MONGO_METHOD(MongoDB, selectCollection)(1, &temp, NULL, getThis(), return_value_used TSRMLS_CC);
    POP_EO_PARAM();
    POP_PARAM(); POP_PARAM();

    collection = &temp;
  }

  MONGO_METHOD(MongoCollection, drop)(0, return_value, return_value_ptr, collection, return_value_used TSRMLS_CC);
}

PHP_METHOD(MongoDB, listCollections) {
  // select db.system.namespaces collection
  zval *nss, *collection, *cursor, *list, *next;

  MAKE_STD_ZVAL(nss);
  ZVAL_STRING(nss, "system.namespaces", 1);

  MAKE_STD_ZVAL(collection);

  PUSH_PARAM(nss); PUSH_PARAM((void*)1);
  PUSH_EO_PARAM();
  MONGO_METHOD(MongoDB, selectCollection)(1, collection, &collection, getThis(), return_value_used TSRMLS_CC);
  POP_EO_PARAM();
  POP_PARAM(); POP_PARAM();
  
  // list to return
  MAKE_STD_ZVAL(list);
  array_init(list);

  // do find  
  MAKE_STD_ZVAL(cursor);
  MONGO_METHOD(MongoCollection, find)(0, cursor, &cursor, collection, return_value_used TSRMLS_CC);
 
  // populate list
  MAKE_STD_ZVAL(next);  
  MONGO_METHOD(MongoCursor, getNext)(0, next, &next, cursor, return_value_used TSRMLS_CC);
  while (Z_TYPE_P(next) != IS_NULL) {
    zval *c, *zname;
    zval **collection;
    char *name, *first_dot, *system;

    // check that the ns is valid and not an index (contains $)
    if (zend_hash_find(HASH_P(next), "name", 5, (void**)&collection) == FAILURE ||
        strchr(Z_STRVAL_PP(collection), '$')) {

      zval_ptr_dtor(&next);
      MAKE_STD_ZVAL(next);

      MONGO_METHOD(MongoCursor, getNext)(0, next, &next, cursor, return_value_used TSRMLS_CC);
      continue;
    }

    first_dot = strchr(Z_STRVAL_PP(collection), '.');
    system = strstr(Z_STRVAL_PP(collection), ".system.");
    // check that this isn't a system ns
    if ((system && first_dot == system) ||
	(name = strchr(Z_STRVAL_PP(collection), '.')) == 0) {

      zval_ptr_dtor(&next);
      MAKE_STD_ZVAL(next);

      MONGO_METHOD(MongoCursor, getNext)(0, next, &next, cursor, return_value_used TSRMLS_CC);
      continue;
    }

    // take a substring after the first "."
    name++;

    MAKE_STD_ZVAL(c);

    MAKE_STD_ZVAL(zname);
    // name must be copied because it is a substring of
    // a string that will be garbage collected in a sec
    ZVAL_STRING(zname, name, 1);

    PUSH_PARAM(zname); PUSH_PARAM((void*)1);
    PUSH_EO_PARAM();
    MONGO_METHOD(MongoDB, selectCollection)(1, c, NULL, getThis(), return_value_used TSRMLS_CC);
    POP_EO_PARAM();
    POP_PARAM(); POP_PARAM();

    add_next_index_zval(list, c);

    zval_ptr_dtor(&zname);
    zval_ptr_dtor(&next);
    MAKE_STD_ZVAL(next);

    MONGO_METHOD(MongoCursor, getNext)(0, next, &next, cursor, return_value_used TSRMLS_CC);
  }

  zval_ptr_dtor(&next);
  zval_ptr_dtor(&nss);
  zval_ptr_dtor(&cursor);
  zval_ptr_dtor(&collection);

  RETURN_ZVAL(list, 0, 1);
}

PHP_METHOD(MongoDB, getCursorInfo) {
  zval *data;
  MAKE_STD_ZVAL(data);
  array_init(data);
  add_assoc_long(data, "cursorInfo", 1);

  PUSH_PARAM(data); PUSH_PARAM((void*)1);
  PUSH_EO_PARAM();
  MONGO_METHOD(MongoDB, command)(1, return_value, &return_value, getThis(), return_value_used TSRMLS_CC);
  POP_EO_PARAM();
  POP_PARAM(); POP_PARAM();

  zval_ptr_dtor(&data);
}

PHP_METHOD(MongoDB, createDBRef) {
  zval *ns, *obj;
  zval **id;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zz", &ns, &obj) == FAILURE) {
    return;
  }

  if (Z_TYPE_P(obj) == IS_ARRAY ||
      Z_TYPE_P(obj) == IS_OBJECT) {
    if (zend_hash_find(HASH_P(obj), "_id", 4, (void**)&id) == SUCCESS) {

      PUSH_PARAM(ns); PUSH_PARAM(*id); PUSH_PARAM((void*)2);
      PUSH_EO_PARAM();
      MONGO_METHOD(MongoDBRef, create)(2, return_value, return_value_ptr, NULL, return_value_used TSRMLS_CC);
      POP_EO_PARAM();
      POP_PARAM(); POP_PARAM(); POP_PARAM();
        
      return;
    }
    else if (Z_TYPE_P(obj) == IS_ARRAY) {
      return;
    }
  }

  PUSH_PARAM(ns); PUSH_PARAM(obj); PUSH_PARAM((void*)2);
  PUSH_EO_PARAM();
  MONGO_METHOD(MongoDBRef, create)(2, return_value, return_value_ptr, NULL, return_value_used TSRMLS_CC);
  POP_EO_PARAM();
  POP_PARAM(); POP_PARAM(); POP_PARAM();
}

PHP_METHOD(MongoDB, getDBRef) {
  zval *ref;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &ref) == FAILURE) {
    return;
  }

  PUSH_PARAM(getThis()); PUSH_PARAM(ref); PUSH_PARAM((void*)2);
  PUSH_EO_PARAM();
  MONGO_METHOD(MongoDBRef, get)(2, return_value, return_value_ptr, NULL, return_value_used TSRMLS_CC);
  POP_EO_PARAM();
  POP_PARAM(); POP_PARAM(); POP_PARAM();
}

PHP_METHOD(MongoDB, execute) {
  int rm_obj = 0;
  zval *code = 0, *args = 0, *zdata;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|a", &code, &args) == FAILURE) {
    return;
  }
  
  if (!args) {
    MAKE_STD_ZVAL(args);
    array_init(args);
  }
  else {
    zval_add_ref(&args);
  }

  // turn the first argument into MongoCode
  if (Z_TYPE_P(code) != IS_OBJECT || 
      Z_OBJCE_P(code) != mongo_ce_Code) {
    zval *obj;

    rm_obj = 1;

    MAKE_STD_ZVAL(obj);
    object_init_ex(obj, mongo_ce_Code);

    PUSH_PARAM(code); PUSH_PARAM((void*)1);
    PUSH_EO_PARAM();
    MONGO_METHOD(MongoCode, __construct)(1, return_value, return_value_ptr, obj, return_value_used TSRMLS_CC);
    POP_EO_PARAM();
    POP_PARAM(); POP_PARAM();

    code = obj;
  }
  else {
    zval_add_ref(&code);
  }

  // create { $eval : code, args : [] }
  MAKE_STD_ZVAL(zdata);
  object_init(zdata);
  add_property_zval(zdata, "$eval", code);
  add_property_zval(zdata, "args", args);

  PUSH_PARAM(zdata); PUSH_PARAM((void*)1);
  PUSH_EO_PARAM();
  MONGO_METHOD(MongoDB, command)(1, return_value, return_value_ptr, getThis(), return_value_used TSRMLS_CC);
  POP_EO_PARAM();
  POP_PARAM(); POP_PARAM();

  if (rm_obj) {  
    zend_objects_store_del_ref(code TSRMLS_CC);
  }
  zval_ptr_dtor(&zdata);
  zval_ptr_dtor(&args);
}

static char *get_cmd_ns(char *db, int db_len) {
  char *position;

  char *cmd_ns = (char*)emalloc(db_len + strlen("$cmd") + 2);
  position = cmd_ns;

  // db
  memcpy(position, db, db_len);
  position += db_len;

  // .
  *(position)++ = '.';

  // $cmd
  memcpy(position, "$cmd", strlen("$cmd"));
  position += strlen("$cmd");

  // \0
  *(position) = '\0';

  return cmd_ns;
}

PHP_METHOD(MongoDB, command) {
  zval ns, temp, limit;
  zval *cmd, *cursor;
  mongo_db *db;
  char *cmd_ns;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &cmd) == FAILURE ||
      IS_SCALAR_P(cmd)) {
    return;
  }

  db = (mongo_db*)zend_object_store_get_object(getThis() TSRMLS_CC);
  MONGO_CHECK_INITIALIZED(db->name, MongoDB);

  // create db.$cmd
  cmd_ns = get_cmd_ns(Z_STRVAL_P(db->name), Z_STRLEN_P(db->name));
  ZVAL_STRING((&ns), cmd_ns, 0);

  // create cursor
  MAKE_STD_ZVAL(cursor);
  object_init_ex(cursor, mongo_ce_Cursor);

  PUSH_PARAM(db->link); PUSH_PARAM(&ns); PUSH_PARAM(cmd); PUSH_PARAM((void*)3);
  PUSH_EO_PARAM();
  MONGO_METHOD(MongoCursor, __construct)(3, &temp, 0, cursor, return_value_used TSRMLS_CC); 
  POP_EO_PARAM();
  POP_PARAM(); POP_PARAM(); POP_PARAM(); POP_PARAM();

  // limit
  Z_TYPE(limit) = IS_LONG;
  Z_LVAL(limit) = -1;

  PUSH_PARAM(&limit); PUSH_PARAM((void*)1);
  PUSH_EO_PARAM();
  MONGO_METHOD(MongoCursor, limit)(1, &temp, NULL, cursor, return_value_used TSRMLS_CC);
  POP_EO_PARAM();
  POP_PARAM(); POP_PARAM();

  // query
  MONGO_METHOD(MongoCursor, getNext)(0, return_value, return_value_ptr, cursor, return_value_used TSRMLS_CC);

  zend_objects_store_del_ref(cursor TSRMLS_CC);
  zval_ptr_dtor(&cursor);
  efree(cmd_ns);
}

static function_entry MongoDB_methods[] = {
  PHP_ME(MongoDB, __construct, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(MongoDB, __toString, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(MongoDB, getGridFS, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(MongoDB, getProfilingLevel, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(MongoDB, setProfilingLevel, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(MongoDB, drop, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(MongoDB, repair, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(MongoDB, selectCollection, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(MongoDB, createCollection, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(MongoDB, dropCollection, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(MongoDB, listCollections, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(MongoDB, getCursorInfo, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(MongoDB, createDBRef, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(MongoDB, getDBRef, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(MongoDB, execute, NULL, ZEND_ACC_PUBLIC)
  PHP_ME(MongoDB, command, NULL, ZEND_ACC_PUBLIC)
  { NULL, NULL, NULL }
};

static void mongo_mongo_db_free(void *object TSRMLS_DC) {
  mongo_db *db = (mongo_db*)object;

  if (db) {
    if (db->link) {
      zval_ptr_dtor(&db->link);
    }
    if (db->name) {
      zval_ptr_dtor(&db->name);
    }

    zend_object_std_dtor(&db->std TSRMLS_CC);
    efree(db);
  }
}


/* {{{ mongo_mongo_db_new
 */
zend_object_value mongo_mongo_db_new(zend_class_entry *class_type TSRMLS_DC) {
  zend_object_value retval;
  mongo_db *intern;
  zval *tmp;

  intern = (mongo_db*)emalloc(sizeof(mongo_db));
  memset(intern, 0, sizeof(mongo_db));

  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  zend_hash_copy(intern->std.properties, &class_type->default_properties, (copy_ctor_func_t) zval_add_ref, 
                 (void *) &tmp, sizeof(zval *));

  retval.handle = zend_objects_store_put(intern, (zend_objects_store_dtor_t) zend_objects_destroy_object, mongo_mongo_db_free, NULL TSRMLS_CC);
  retval.handlers = &mongo_default_handlers;

  return retval;
}
/* }}} */


void mongo_init_MongoDB(TSRMLS_D) {
  zend_class_entry ce;

  INIT_CLASS_ENTRY(ce, "MongoDB", MongoDB_methods);
  ce.create_object = mongo_mongo_db_new;
  mongo_ce_DB = zend_register_internal_class(&ce TSRMLS_CC);

  zend_declare_class_constant_long(mongo_ce_DB, "PROFILING_OFF", strlen("PROFILING_OFF"), 0 TSRMLS_CC);
  zend_declare_class_constant_long(mongo_ce_DB, "PROFILING_SLOW", strlen("PROFILING_SLOW"), 1 TSRMLS_CC);
  zend_declare_class_constant_long(mongo_ce_DB, "PROFILING_ON", strlen("PROFILING_ON"), 2 TSRMLS_CC);
}
