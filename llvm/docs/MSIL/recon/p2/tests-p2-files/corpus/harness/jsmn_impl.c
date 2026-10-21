/* The one translation unit that defines jsmn's functions (jsmn.h without
   JSMN_HEADER). Every TU must agree on JSMN_PARENT_LINKS and JSMN_STRICT,
   which change the layout of jsmntok_t. */
#include "jsmn.h"
