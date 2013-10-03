/*
* Copyright (C) 2013 Lourival Vieira Neto <lourival.neto@gmail.com>.
* All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
* 
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
* 
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <lua.h>
#include <lauxlib.h>

#include <arpa/inet.h>

#include "bit_util.h"

#define DEBUG
// TODO: create DEBUG(...) macro!

// TODO:  - clean up!
//        - review int types!
//        - gc
//        - improve limits and other verifications
// NEXT_VERSION:
//        - named fields
//        - implement set and rawset

uint8_t buffer[ 4 ] = { 0xFF, 0x00, 0xFF, 0x00 };

typedef struct {
  uint8_t alignment;
  void *  buffer;
  size_t  size;
  bool    net;
} lbuf_t;

typedef struct {
// TODO: use two ix levels (bit and align (e.g., byte))?
  uint32_t offset;
  uint8_t  length;
} mask_t;

// TODO: pass a free function
lbuf_t * lbuf_new(lua_State * L, void * buffer, size_t size)
{
  lbuf_t * lbuf = lua_newuserdata(L, sizeof(lbuf_t));
  lbuf->buffer  = buffer;
  lbuf->size    = size;
  lbuf->alignment = 8;
  lbuf->net = true;
  luaL_getmetatable(L, "lbuf");
  lua_setmetatable(L, -2);
  return lbuf;
}

// TODO: implement lbuf.new(string | table)
static int lbuf_new_(lua_State *L)
{
  lbuf_t * lbuf = lbuf_new(L, buffer, 4);

#ifdef DEBUG
  printf("[%s:%s:%d]: buffer: ", __FUNCTION__, __FILE__, __LINE__);
  for (int i = 0; i < 4; i++) printf("%#x, ", (int) buffer[ i ]);
  printf("\n");
#endif
  return 1;
}

static int lbuf_mask(lua_State *L)
{
  // TODO: create and use isudata()
  lbuf_t * lbuf = (lbuf_t *) luaL_checkudata(L, 1, "lbuf");

  if (lua_isnumber(L, 2)) {
      // TODO: when set align, erase mask table!
    lbuf->alignment = (uint8_t) lua_tointeger(L, 2);
    lua_pop(L, 1);
    return 1;
  }
  else if (!lua_istable(L, 2)) {
    lua_pushnil(L);
    return 1;
  }

  // TODO: create a new clean table to put masks!
  int t = 2;
  uint8_t offset = 0;
  /* table is in the stack at index 't' */
  lua_pushnil(L);  /* first key */
  while (lua_next(L, t) != 0) {
    /* uses 'key' (at index -2) and 'value' (at index -1) */
    if (!lua_isnumber(L, -1)) {
      lua_pop(L, 1);
      continue;
    }

    uint8_t length = lua_tonumber(L, -1);
    if (offset + length > lbuf->size * 8) { 
      lua_pop(L, 2);
      break;
    }

    lua_pushvalue(L, -2); // key
    //TODO: check if null
    mask_t * mask = lua_newuserdata(L, sizeof(mask_t)); 
    luaL_getmetatable(L, "lbuf.mask");
    lua_setmetatable(L, -2);

    mask->offset = offset;
    // TODO: also handle tables
    mask->length = length;
    offset       = mask->offset + mask->length;

    lua_settable(L, 2); // mask_table[ k ] = mask

    /* removes 'value'; keeps 'key' for next iteration */
    lua_pop(L, 1);
  }

  lua_getmetatable(L, 1);
  lua_getfield(L, -1, "__masks");
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_settable(L, -3); 

  // TODO: define what we should return
  lua_pushvalue(L, 1);
  return 1;
}

static int lbuf_gc(lua_State *L)
{
  lbuf_t * lbuf = (lbuf_t *) luaL_checkudata(L, 1, "lbuf");

  // remove entry from masks table (mt.masks[self] = nil)
  lua_getmetatable(L, 1);
  lua_getfield(L, -1, "__masks");
  lua_pushvalue(L, 1);
  lua_pushnil(L);
  lua_settable(L, -3);

  // TODO: free lbuf->buffer if ref count == 0
  return 0;
}

static int lbuf_get(lua_State *L)
{
  lbuf_t * lbuf = (lbuf_t *) luaL_checkudata(L, 1, "lbuf");

  lua_getmetatable(L, 1);
  
  // access to methods (such as mask())
  if (lua_isstring(L, 2)) {
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    if (lua_iscfunction(L, -1))
      return 1;
    else
      lua_pop(L, 1);
  }

  lua_getfield(L, -1, "__masks");
  lua_pushvalue(L, 1); // [ lubf | __masks | getmetatable(lbuf) | ix | lbuf ]
  lua_gettable(L, -2); // [ __masks[ lbuf ] | lbuf | __masks | (...) ]
  if (!lua_istable(L, -1)) {
    uint8_t ix = (uint8_t) luaL_checkinteger(L, 2);
    if (ix < 1 || ix * lbuf->alignment > lbuf->size * 8) { 
      lua_pushnil(L);
      return 1;
    }

    // TODO: create get_offset macro
    uint32_t offset = (ix - 1) * lbuf->alignment;
    int64_t  value = get_bits(lbuf->buffer, offset, lbuf->alignment, lbuf->net);
    lua_pushnumber(L, value);
    return 1;
  }

  lua_pushvalue(L, 2); // [ ix | __masks[ lbuf ] | (...) ]
  lua_gettable(L, -2); // [ __masks[ lbuf ][ ix ] | ix | __masks[ lbuf ] | (...) ]
  if (lua_isnil(L, -1)) 
    return 1;

  mask_t * mask = (mask_t *) luaL_checkudata(L, -1, "lbuf.mask");

  int64_t value = get_bits(lbuf->buffer, mask->offset, mask->length, lbuf->net);
  lua_pushnumber(L, value);
  return 1;
}

static int lbuf_set(lua_State *L)
{
  lbuf_t * lbuf = (lbuf_t *) luaL_checkudata(L, 1, "lbuf");
  int64_t value = (int64_t) lua_tointeger(L, 3);

  lua_getmetatable(L, 1);

  lua_getfield(L, -1, "__masks");
  lua_pushvalue(L, 1); // [ lubf | __masks | getmetatable(lbuf) | ix | lbuf ]
  lua_gettable(L, -2); // [ __masks[ lbuf ] | lbuf | __masks | (...) ]
  if (!lua_istable(L, -1)) {
    uint8_t ix = (uint8_t) luaL_checkinteger(L, 2);
    if (ix < 1 || ix * lbuf->alignment > lbuf->size * 8) { 
      return 0;
    }

    uint32_t offset = (ix - 1) * lbuf->alignment;
    set_bits(lbuf->buffer, offset, lbuf->alignment, lbuf->net, value);
    return 0;
  }

  lua_pushvalue(L, 2); // [ ix | __masks[ lbuf ] | (...) ]
  lua_gettable(L, -2); // [ __masks[ lbuf ][ ix ] | ix | __masks[ lbuf ] | (...) ]
  if (lua_isnil(L, -1)) 
    return 0;

  mask_t * mask = (mask_t *) luaL_checkudata(L, -1, "lbuf.mask");

  set_bits(lbuf->buffer, mask->offset, mask->length, lbuf->net, value);
  return 0;
}

static int lbuf_rawget(lua_State *L)
{
  lbuf_t * lbuf = (lbuf_t *) luaL_checkudata(L, 1, "lbuf");
  uint8_t ix  = (uint8_t) luaL_checkinteger(L, 2);
  uint8_t len = (uint8_t) luaL_checkinteger(L, 3);

  if (ix < 0 || ix + len > lbuf->size * 8) { 
    lua_pushnil(L);
    return 1;
  }

  int64_t value = get_bits(lbuf->buffer, ix, len, lbuf->net);
  lua_pushnumber(L, value);
  return 1;
}

// TODO: implement rawset!

static const luaL_Reg lbuf[ ] = {
  {"new", lbuf_new_},
  {"mask", lbuf_mask},
  {"rawget", lbuf_rawget},
  {NULL, NULL}
};

static const luaL_Reg lbuf_m[ ] = {
  {"mask", lbuf_mask},
  {"rawget", lbuf_rawget},
  {"__index", lbuf_get},
  {"__newindex", lbuf_set},
  {"__gc", lbuf_gc},
  {NULL, NULL}
};

static const luaL_Reg mask_m[ ] = {
  {NULL, NULL}
};

int luaopen_lbuf(lua_State *L)
{
  luaL_newmetatable(L, "lbuf");
  luaL_register(L, NULL, lbuf_m);
  lua_newtable(L);
  lua_setfield(L, -2, "__masks");
  luaL_register(L, "lbuf", lbuf);

  luaL_newmetatable(L, "lbuf.mask");
  luaL_register(L, NULL, mask_m);
  return 1;
}

