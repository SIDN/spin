#define LUA_LIB
#include "lua.h"
#include "lauxlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <libnetfilter_log/linux_nfnetlink_log.h>

#include <libnfnetlink/libnfnetlink.h>
#include <libnetfilter_log/libnetfilter_log.h>


#include <math.h>
#define LUA_MATHLIBNAME "myname"
#define PI 3.1415729

static int math_sin (lua_State *L) {
    //char* a = malloc(24000);
    //(void)a;
    //lua_pushnumber(L, sin(luaL_checknumber(L, 1)));
    lua_pushnumber(L, 32);
    return 1;
}

#define BUFSZ 10000

void stackdump_g(lua_State* l)
{
    int i;
    int top = lua_gettop(l);
    printf("--------------stack--------------\n");
    printf("total in stack %d\n",top);

    for (i = 1; i <= top; i++)
    {  /* repeat for each level */
        int t = lua_type(l, i);
        switch (t) {
            case LUA_TSTRING:  /* strings */
                printf("string: '%s'\n", lua_tostring(l, i));
                break;
            case LUA_TBOOLEAN:  /* booleans */
                printf("boolean %s\n",lua_toboolean(l, i) ? "true" : "false");
                break;
            case LUA_TNUMBER:  /* numbers */
                printf("number: %g\n", lua_tonumber(l, i));
                break;
            default:  /* other values */
                printf("%s\n", lua_typename(l, t));
                break;
        }
        printf("  ");  /* put a separator */
    }
    printf("\n");  /* end the listing */
    printf("----------end  stack-------------\n");
}

typedef struct {
    // callback function passed by the user, stored in the lua registry
    int lua_callback_regid;
    // optional data passed by the user, stored in the lua registry
    int lua_callback_data_regid;
    // file descriptor of the netfilter listener
    int fd;
    // netlogger handle
    struct nflog_handle *handle;
    // netlogger group handle
    struct nflog_g_handle *ghandle;
    // lua state stack, used when calling the callback
    lua_State* L;
} netlogger_info;

int callback_handler(struct nflog_g_handle *handle,
                     struct nfgenmsg *msg,
                     struct nflog_data *nfldata,
                     void *netlogger_info_ptr) {
    printf("callback handler called\n");
    netlogger_info* nli = (netlogger_info*) netlogger_info_ptr;

    lua_rawgeti(nli->L, LUA_REGISTRYINDEX, nli->lua_callback_regid);
    lua_pushnumber(nli->L, 1);
    lua_pushnumber(nli->L, 2);
    lua_pushnumber(nli->L, 3);

    //stackdump_g(nli->L);
    int result = lua_pcall(nli->L, 3, 0, 0);
    //stackdump_g(nli->L);
    printf("[XX] CALLBACK RESULT: %d\n", result);
    if (result != 0) {
        printf("ERROR: %s\n", lua_tostring(nli->L, 1));
    }
}

// Library function mapping
static const luaL_Reg mathlib[] = {
    {"sin", math_sin},
    {"setup_netlogger_loop", setup_netlogger_loop},
//    {"loop_forever", loop_forever},
//    {"loop_once", loop_once},
//    {"close_netlogger", close_netlogger},
    {NULL, NULL}
};

// Handler function mapping
static const luaL_Reg handler_mapping[] = {
    {"loop_forever", loop_forever},
    {"loop_once", loop_once},
    {"close_netlogger", close_netlogger},
    {NULL, NULL}
};



//
// Sets up a loop
// arguments:
// group_number (int)
// callback_function (function)
// optional callback function extra argument
//
// The callback function will be passed a pointer to the event and the optional extra user argument
//
// This function returns the *group* handle
static int setup_netlogger_loop(lua_State *L) {
    int fd = -1;

    struct nflog_handle *handle = NULL;
    struct nflog_g_handle *group = NULL;
    netlogger_info* nli = (netlogger_info*) malloc(sizeof(netlogger_info));
    printf("[XX] alloced %u bytes at %p\n", sizeof(netlogger_info), nli);
    // handle the lua arugments to this function
    int groupnum = luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    // create a new state, add the function to it, then add the arguments as they come along
    // Make a reference from the userdata table and the callback functions;
    // we will push these on the stack when performing the callback.
    nli->L = L;
    nli->lua_callback_data_regid = luaL_ref(L, LUA_REGISTRYINDEX);
    nli->lua_callback_regid = luaL_ref(L, LUA_REGISTRYINDEX);

    /* This opens the relevent netlink socket of the relevent type */
    if ((handle = nflog_open()) == NULL){
        fprintf(stderr, "Could not get netlink handle\n");
        exit(1);
    }

    if (nflog_bind_pf(handle, AF_INET) < 0) {
        fprintf(stderr, "Could not bind netlink handle\n");
        exit(1);
    }

    if ((group = nflog_bind_group(handle, groupnum)) == NULL) {
        fprintf(stderr, "Could not bind to group\n");
        exit(1);
    }

    if (nflog_set_mode(group, NFULNL_COPY_PACKET, 0xffff) < 0) {
        fprintf(stderr, "Could not set group mode\n");
        exit(1);
    }
    if (nflog_set_nlbufsiz(group, BUFSZ) < 0) {
        fprintf(stderr, "Could not set group buffer size\n");
        exit(1);
    }
    if (nflog_set_timeout(group, 1) < 0) {
        fprintf(stderr, "Could not set the group timeout\n");
    }

    nflog_callback_register(group, &callback_handler, nli);

    //lua_pushcfunction(L, g);

    /* Get the actual FD for the netlogger entry */
    fd = nflog_fd(handle);

    struct timeval tv;
    tv.tv_sec = 1;  /* 30 Secs Timeout */
    tv.tv_usec = 0;  // Not init'ing this can cause strange errors
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv,sizeof(struct timeval));

    /* We continually read from the loop and push the contents into
    nflog_handle_packet (which seperates one entry from the other),
    which will eventually invoke our callback (queue_push) */
    /*
    for (;;) {
    sz = recv(fd, buf, BUFSZ, 0);
    if (sz < 0 && (errno == EINTR || errno == EAGAIN)) {
        continue;
    } else if (sz < 0) {
        printf("Error reading from nflog socket\n");
        break;
    }
        nflog_handle_packet(handle, buf, sz);
    }
*/
    nli->fd = fd;
    nli->handle = handle;


    printf("[XX] created nli at %p\n", nli);
    lua_pushlightuserdata(L, nli);
    return 1;
}

static int close_netlogger(lua_State *L) {
    netlogger_info* nli = (netlogger_info*) lua_touserdata(L, 1);

    nflog_unbind_group(nli->ghandle);
    nflog_close(nli->handle);
    printf("[XX] free %u bytes at %p", sizeof(netlogger_info), nli);
    free(nli);

    lua_pushnil(L);
    return 1;
}


static int loop_once(lua_State *L) {
    int sz;
    //int fd = -1;
    char buf[BUFSZ];

    netlogger_info* nli = (netlogger_info*) lua_touserdata(L, 1);
    printf("[XX] got nli at %p\n", nli);
    printf("[XX] read\n");
    sz = recv(nli->fd, buf, BUFSZ, 0);
    if (sz < 0 && (errno == EINTR || errno == EAGAIN)) {
        printf("[XX] EINTR or EAGAIN\n", nli);
        lua_pushnil(L);
        return 1;
    } else if (sz < 0) {
        printf("Error reading from nflog socket\n");
        lua_pushnil(L);
        return 1;
    }
    printf("[XX] call handle packet\n", nli);
    nflog_handle_packet(nli->handle, buf, sz);

    lua_pushnil(L);
    return 1;
}

static int loop_forever(lua_State *L) {
    int sz;
    //int fd = -1;
    char buf[BUFSZ];

    netlogger_info* nli = (netlogger_info*) lua_touserdata(L, 1);
    printf("[XX] got nli at %p\n", nli);
    for (;;) {
        printf("[XX] read\n");
        sz = recv(nli->fd, buf, BUFSZ, 0);
        if (sz < 0 && (errno == EINTR || errno == EAGAIN)) {
            printf("[XX] EINTR or EAGAIN\n", nli);

            continue;
        } else if (sz < 0) {
            printf("Error reading from nflog socket\n");
            break;
        }
        printf("[XX] call handle packet\n", nli);
        nflog_handle_packet(nli->handle, buf, sz);
    }
    lua_pushnil(L);
    return 1;
}


    luaL_newmetatable(L, "LuaBook.testclass"); //leaves new metatable on the stack
    lua_pushvalue(L, -1); // there are two 'copies' of the metatable on the stack
    lua_setfield(L, -2, "__index"); // pop one of those copies and assign it to
                                    // __index field od the 1st metatable
    luaL_register(L, NULL, arraylib_m); // register functions in the metatable
    luaL_register(L, "testclass", arraylib_f);

/*
** Netfilter-log library
*/
LUALIB_API int luaopen_lnflog (lua_State *L) {
    luaL_register(L, LUA_MATHLIBNAME, mathlib);
    lua_pushnumber(L, PI);
    lua_setfield(L, -2, "pi");
    lua_pushnumber(L, HUGE_VAL);
    lua_setfield(L, -2, "huge");

    // any additional initialization code goes here

    return 1;
}
