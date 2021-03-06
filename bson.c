// bson.c
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

#ifdef WIN32
#include <memory.h>
#endif

#include "php_mongo.h"
#include "bson.h"
#include "mongo_types.h"

extern zend_class_entry *mongo_ce_BinData,
  *mongo_ce_Code,
  *mongo_ce_Date,
  *mongo_ce_Id,
  *mongo_ce_Regex,
  *mongo_ce_EmptyObj,
  *mongo_ce_Exception;

ZEND_EXTERN_MODULE_GLOBALS(mongo);

static int prep_obj_for_db(buffer *buf, HashTable *array TSRMLS_DC);
#if ZEND_MODULE_API_NO >= 20090115
static int apply_func_args_wrapper(void **data TSRMLS_DC, int num_args, va_list args, zend_hash_key *key);
#else
static int apply_func_args_wrapper(void **data, int num_args, va_list args, zend_hash_key *key);
#endif /* ZEND_MODULE_API_NO >= 20090115 */


static int prep_obj_for_db(buffer *buf, HashTable *array TSRMLS_DC) {
  zval temp, **data, *newid;

  // if _id field doesn't exist, add it
  if (zend_hash_find(array, "_id", 4, (void**)&data) == FAILURE) {
    // create new MongoId
    MAKE_STD_ZVAL(newid);
    object_init_ex(newid, mongo_ce_Id);
    MONGO_METHOD(MongoId, __construct)(0, &temp, NULL, newid, 0 TSRMLS_CC);

    // add to obj
    zend_hash_add(array, "_id", 4, &newid, sizeof(zval*), NULL);

    // set to data
    data = &newid;
  }

  serialize_element("_id", data, buf, 0 TSRMLS_CC);

  return SUCCESS;
}


// serialize a zval
int zval_to_bson(buffer *buf, HashTable *hash, int prep TSRMLS_DC) {
  uint start;
  int num = 0;

  // check buf size
  if(BUF_REMAINING <= 5) {
    resize_buf(buf, 5);
  }

  // keep a record of the starting position
  // as an offset, in case the memory is resized
  start = buf->pos-buf->start;

  // skip first 4 bytes to leave room for size
  buf->pos += INT_32;

  if (zend_hash_num_elements(hash) > 0) {
    if (prep) {
      prep_obj_for_db(buf, hash TSRMLS_CC);
      num++;
    }
  
#if ZEND_MODULE_API_NO >= 20090115
    zend_hash_apply_with_arguments(hash TSRMLS_CC, (apply_func_args_t)apply_func_args_wrapper, 3, buf, prep, &num);
#else
    zend_hash_apply_with_arguments(hash, (apply_func_args_t)apply_func_args_wrapper, 4, buf, prep, &num TSRMLS_CC);
#endif /* ZEND_MODULE_API_NO >= 20090115 */
  }

  serialize_null(buf);
  serialize_size(buf->start+start, buf);
  return num;
}

#if ZEND_MODULE_API_NO >= 20090115
static int apply_func_args_wrapper(void **data TSRMLS_DC, int num_args, va_list args, zend_hash_key *key) {
#else
static int apply_func_args_wrapper(void **data, int num_args, va_list args, zend_hash_key *key) {
#endif /* ZEND_MODULE_API_NO >= 20090115 */

  int retval;
  char *name;

  buffer *buf = va_arg(args, buffer*);
  int prep = va_arg(args, int);
  int *num = va_arg(args, int*);

#if ZEND_MODULE_API_NO < 20090115
  void ***tsrm_ls = va_arg(args, void***);
#endif /* ZEND_MODULE_API_NO < 20090115 */

  if (key->nKeyLength) {
    return serialize_element(key->arKey, (zval**)data, buf, prep TSRMLS_CC);
  }

  spprintf(&name, 0, "%ld", key->h);
  retval = serialize_element(name, (zval**)data, buf, prep TSRMLS_CC);
  efree(name);

  // if the key is a number in ascending order, we're still
  // dealing with an array, not an object, so increase the count
  if (key->h == *num) {
    (*num)++;
  }

  return retval;
}

int serialize_element(char *name, zval **data, buffer *buf, int prep TSRMLS_DC) {
  int name_len = strlen(name);

  if (prep && strcmp(name, "_id") == 0) {
    return ZEND_HASH_APPLY_KEEP;
  }

  switch (Z_TYPE_PP(data)) {
  case IS_NULL:
    set_type(buf, BSON_NULL);
    serialize_string(buf, name, name_len);
    break;
  case IS_LONG:
    set_type(buf, BSON_INT);
    serialize_string(buf, name, name_len);
    serialize_int(buf, Z_LVAL_PP(data));
    break;
  case IS_DOUBLE:
    set_type(buf, BSON_DOUBLE);
    serialize_string(buf, name, name_len);
    serialize_double(buf, Z_DVAL_PP(data));
    break;
  case IS_BOOL:
    set_type(buf, BSON_BOOL);
    serialize_string(buf, name, name_len);
    serialize_bool(buf, Z_BVAL_PP(data));
    break;
  case IS_STRING: {
    set_type(buf, BSON_STRING);
    serialize_string(buf, name, name_len);

    serialize_int(buf, Z_STRLEN_PP(data)+1);
    serialize_string(buf, Z_STRVAL_PP(data), Z_STRLEN_PP(data));
    break;
  }
  case IS_ARRAY: {
    int num;

    // if we realloc, we need an offset, not an abs pos (phew)
    int type_offset = buf->pos-buf->start;
    // skip type until we know whether it was an array or an object
    buf->pos++;

    //serialize
    serialize_string(buf, name, name_len);
    num = zval_to_bson(buf, Z_ARRVAL_PP(data), NO_PREP TSRMLS_CC);

    // now go back and set the type bit
    //set_type(buf, BSON_ARRAY);
    if (num == zend_hash_num_elements(Z_ARRVAL_PP(data))) {
      buf->start[type_offset] = BSON_ARRAY;
    }
    else {
      buf->start[type_offset] = BSON_OBJECT;
    }

    break;
  }
  case IS_OBJECT: {
    zend_class_entry *clazz = Z_OBJCE_PP( data );
    /* check for defined classes */
    // MongoId
    if(clazz == mongo_ce_Id) {
      mongo_id *id;

      set_type(buf, BSON_OID);
      serialize_string(buf, name, name_len);
      id = (mongo_id*)zend_object_store_get_object(*data TSRMLS_CC);
      if (!id->id) {
	return ZEND_HASH_APPLY_KEEP;
      }

      serialize_bytes(buf, id->id, OID_SIZE);
    }
    // MongoDate
    else if (clazz == mongo_ce_Date) {
      zval *zsec, *zusec;
      long long int sec, usec, ms;

      set_type(buf, BSON_DATE);
      serialize_string(buf, name, name_len);

      zsec = zend_read_property(mongo_ce_Date, *data, "sec", 3, 0 TSRMLS_CC);
      sec = Z_LVAL_P(zsec);
      zusec = zend_read_property(mongo_ce_Date, *data, "usec", 4, 0 TSRMLS_CC);
      usec = Z_LVAL_P(zusec);

      ms = (sec * 1000) + (usec / 1000);
      serialize_long(buf, ms);
    }
    // MongoRegex
    else if (clazz == mongo_ce_Regex) {
      zval *zid;

      set_type(buf, BSON_REGEX);
      serialize_string(buf, name, name_len);
      zid = zend_read_property(mongo_ce_Regex, *data, "regex", 5, 0 TSRMLS_CC);
      serialize_string(buf, Z_STRVAL_P(zid), Z_STRLEN_P(zid));
      zid = zend_read_property(mongo_ce_Regex, *data, "flags", 5, 0 TSRMLS_CC);
      serialize_string(buf, Z_STRVAL_P(zid), Z_STRLEN_P(zid));
    }
    // MongoCode
    else if (clazz == mongo_ce_Code) {
      uint start;
      zval *zid;

      set_type(buf, BSON_CODE);
      serialize_string(buf, name, name_len);

      // save spot for size
      start = buf->pos-buf->start;
      buf->pos += INT_32;
      zid = zend_read_property(mongo_ce_Code, *data, "code", 4, NOISY TSRMLS_CC);
      // string size
      serialize_int(buf, Z_STRLEN_P(zid)+1);
      // string
      serialize_string(buf, Z_STRVAL_P(zid), Z_STRLEN_P(zid));
      // scope
      zid = zend_read_property(mongo_ce_Code, *data, "scope", 5, NOISY TSRMLS_CC);
      zval_to_bson(buf, HASH_P(zid), NO_PREP TSRMLS_CC);

      // get total size
      serialize_size(buf->start+start, buf);
    }
    // MongoBin
    else if (clazz == mongo_ce_BinData) {
      zval *zbin, *ztype;

      set_type(buf, BSON_BINARY);
      serialize_string(buf, name, name_len);

      zbin = zend_read_property(mongo_ce_BinData, *data, "bin", 3, 0 TSRMLS_CC);
      serialize_int(buf, Z_STRLEN_P(zbin));

      ztype = zend_read_property(mongo_ce_BinData, *data, "type", 4, 0 TSRMLS_CC);
      serialize_byte(buf, (unsigned char)Z_LVAL_P(ztype));

      if(BUF_REMAINING <= Z_STRLEN_P(zbin)) {
        resize_buf(buf, Z_STRLEN_P(zbin));
      }

      serialize_bytes(buf, Z_STRVAL_P(zbin), Z_STRLEN_P(zbin));
    }
    // serialize a normal obj
    else {
      HashTable *hash = Z_OBJPROP_PP(data);

      // go through the k/v pairs and serialize them
      set_type(buf, BSON_OBJECT);
      serialize_string(buf, name, name_len);

      zval_to_bson(buf, hash, NO_PREP TSRMLS_CC);
    } 
    break;
  }
  }

  return ZEND_HASH_APPLY_KEEP;
}

int resize_buf(buffer *buf, int size) {
  int total = buf->end - buf->start;
  int used = buf->pos - buf->start;

  total = total < GROW_SLOWLY ? total*2 : total+INITIAL_BUF_SIZE;
  while (total-used < size) {
    total += size;
  }

  buf->start = (unsigned char*)erealloc(buf->start, total);
  buf->pos = buf->start + used;
  buf->end = buf->start + total;
  return total;
}

inline void serialize_byte(buffer *buf, char b) {
  if(BUF_REMAINING <= 1) {
    resize_buf(buf, 1);
  }
  *(buf->pos) = b;
  buf->pos += 1;
}

inline void serialize_bytes(buffer *buf, char *str, int str_len) {
  if(BUF_REMAINING <= str_len) {
    resize_buf(buf, str_len);
  }
  memcpy(buf->pos, str, str_len);
  buf->pos += str_len;
}

inline void serialize_string(buffer *buf, char *str, int str_len) {
  if(BUF_REMAINING <= str_len+1) {
    resize_buf(buf, str_len+1);
  }

  memcpy(buf->pos, str, str_len);
  // add \0 at the end of the string
  buf->pos[str_len] = 0;
  buf->pos += str_len + 1;
}

inline void serialize_int(buffer *buf, int num) {
  if(BUF_REMAINING <= INT_32) {
    resize_buf(buf, INT_32);
  }
  memcpy(buf->pos, &num, INT_32);
  buf->pos += INT_32;
}

inline void serialize_long(buffer *buf, long long num) {
  if(BUF_REMAINING <= INT_64) {
    resize_buf(buf, INT_64);
  }
  memcpy(buf->pos, &num, INT_64);
  buf->pos += INT_64;
}

inline void serialize_double(buffer *buf, double num) {
  if(BUF_REMAINING <= INT_64) {
    resize_buf(buf, INT_64);
  }
  memcpy(buf->pos, &num, DOUBLE_64);
  buf->pos += DOUBLE_64;
}

/* the position is not increased, we are just filling
 * in the first 4 bytes with the size.
 */
inline void serialize_size(unsigned char *start, buffer *buf) {
  unsigned int total = buf->pos - start;
  memcpy(start, &total, INT_32);
}


char* bson_to_zval(char *buf, HashTable *result TSRMLS_DC) {
  char type;

  // for size
  buf += INT_32;
  
  while ((type = *buf++) != 0) {
    char *name;
    zval *value;
    
    name = buf;
    // get past field name
    buf += strlen(buf) + 1;

    MAKE_STD_ZVAL(value);
    
    // get value
    switch(type) {
    case BSON_OID: {
      mongo_id *this_id;

      object_init_ex(value, mongo_ce_Id);

      this_id = (mongo_id*)zend_object_store_get_object(value TSRMLS_CC);
      this_id->id = estrndup(buf, OID_SIZE);

      buf += OID_SIZE;
      break;
    }
    case BSON_DOUBLE: {
      ZVAL_DOUBLE(value, *(double*)buf);
      buf += DOUBLE_64;
      break;
    }
    case BSON_STRING: {
      // len includes \0
      int len = *((int*)buf);
      buf += INT_32;

      ZVAL_STRINGL(value, buf, len-1, 1);
      buf += len;
      break;
    }
    case BSON_OBJECT: 
    case BSON_ARRAY: {
      array_init(value);
      buf = bson_to_zval(buf, Z_ARRVAL_P(value) TSRMLS_CC);
      break;
    }
    case BSON_BINARY: {
      int len = *(int*)buf;
      char type, *bytes;

      buf += INT_32;

      type = *buf++;

      bytes = buf;
      buf += len;

      object_init_ex(value, mongo_ce_BinData);

      add_property_stringl(value, "bin", bytes, len, DUP);
      add_property_long(value, "type", type);
      break;
    }
    case BSON_BOOL: {
      char d = *buf++;
      ZVAL_BOOL(value, d);
      break;
    }
    case BSON_UNDEF:
    case BSON_NULL: {
      ZVAL_NULL(value);
      break;
    }
    case BSON_INT: {
      ZVAL_LONG(value, *((int*)buf));
      buf += INT_32;
      break;
    }
    case BSON_LONG: {
      ZVAL_DOUBLE(value, (double)*((long long int*)buf));
      buf += INT_64;
      break;
    }
    case BSON_DATE: {
      long long int d = *((long long int*)buf);
      buf += INT_64;
      
      object_init_ex(value, mongo_ce_Date);

      add_property_long(value, "sec", (long)(d/1000));
      add_property_long(value, "usec", (d*1000)%1000000);

      break;
    }
    case BSON_REGEX: {
      char *regex, *flags;
      int regex_len, flags_len;

      regex = buf;
      regex_len = strlen(buf);
      buf += regex_len+1;

      flags = buf;
      flags_len = strlen(buf);
      buf += flags_len+1;

      object_init_ex(value, mongo_ce_Regex);

      add_property_stringl(value, "regex", regex, regex_len, 1);
      add_property_stringl(value, "flags", flags, flags_len, 1);

      break;
    }
    case BSON_CODE: 
    case BSON_CODE__D: {
      zval *zcope;
      int code_len;
      char *code;

      object_init_ex(value, mongo_ce_Code);
      // initialize scope array
      MAKE_STD_ZVAL(zcope);
      array_init(zcope);

      // CODE has a useless total size field
      if (type == BSON_CODE) {
        buf += INT_32;
      }

      // length of code (includes \0)
      code_len = *(int*)buf;
      buf += INT_32;

      code = buf;
      buf += code_len;

      if (type == BSON_CODE) {
        buf = bson_to_zval(buf, HASH_P(zcope) TSRMLS_CC);
      }

      // exclude \0
      add_property_stringl(value, "code", code, code_len-1, DUP);
      add_property_zval(value, "scope", zcope);

      // somehow, we pick up an extra zcope ref
      zval_ptr_dtor(&zcope);
      break;
    }
    case BSON_DBREF: {
      int ns_len;
      char *ns;
      zval *zoid;
      mongo_id *this_id;

      // ns
      ns_len = *(int*)buf;
      buf += INT_32;
      ns = buf;
      buf += ns_len;

      // id
      MAKE_STD_ZVAL(zoid);
      object_init_ex(zoid, mongo_ce_Id);

      this_id = (mongo_id*)zend_object_store_get_object(zoid TSRMLS_CC);
      this_id->id = estrndup(buf, OID_SIZE);

      buf += OID_SIZE;

      // put it all together
      array_init(value);
      add_assoc_stringl(value, "$ref", ns, ns_len-1, 1);
      add_assoc_zval(value, "$id", zoid);
      break;
    }
    case BSON_TIMESTAMP: {
      long long int d;

      d = *(int*)buf;
      buf += INT_64;

      ZVAL_LONG(value, (long)d);
      break;
    }
    case BSON_MINKEY: {
      ZVAL_STRING(value, "[MinKey]", 1);
      break;
    }
    case BSON_MAXKEY: {
      ZVAL_STRING(value, "[MaxKey]", 1);
      break;
    }
    default: {
      php_printf("type %d not supported\n", type);
      // give up, it'll be trouble if we keep going
      return buf;
    }
    }

    zend_symtable_update(result, name, strlen(name)+1, &value, sizeof(zval*), NULL);
  }

  return buf;
}

