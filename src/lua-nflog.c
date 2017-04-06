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

int callback_handler(struct nflog_g_handle *handle,
                     struct nfgenmsg *msg,
                     struct nflog_data *nfldata,
                     void *lua_state) {

}

struct callback_data {
    lua_State* CBL;
};


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
    int sz;
    int fd = -1;
    char buf[BUFSZ];
    /* Setup handle */
    struct nflog_handle *handle = NULL;
    struct nflog_g_handle *group = NULL;
    struct callback_data cbd;

    int groupnum = luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    // create a new state, add the function to it, then add the arguments as they come along
    stackdump_g(L);
    // Make a reference from the userdata table and the callback functions;
    // we will push these on the stack when performing the callback.
    int udata = luaL_ref(L, LUA_REGISTRYINDEX);
    int callback = luaL_ref(L, LUA_REGISTRYINDEX);
    printf("Udata ref: %d   cb ref: %d\n", udata, callback);
    stackdump_g(L);

    lua_rawgeti(L, LUA_REGISTRYINDEX, callback);
/*
    lua_pushnumber(L, 1);
    lua_pushnumber(L, 2);
    lua_pushnumber(L, 3);
*/
/*
    lua_State* tmp = luaL_newstate();
    // 1 should be top - 2
    lua_xmove(L, tmp, stacktop - 2);

    cbd.L = luaL_newstate();
    lua_xmove(L, cbd.L, 1);
    lua_xmove(tmp, L, lua_gettop(tmp));


    luaL_checktype(cbd.L, 1, LUA_TFUNCTION);
*/
    stackdump_g(L);
    int result = lua_pcall(L, 0, 0, 0);
    stackdump_g(L);
    printf("[XX] CALLBACK RESULT: %d\n", result);
    if (result != 0) {
        printf("ERROR: %s\n", lua_tostring(L, 1));
    }
/*
    //const void* cb_function = lua_topointer(L, 2);
    //luaL_checktype(L, 3, LUA_TTABLE);
    // should we copy this? can it go out of scope?
    //const void* cb_data = lua_topointer(L, 3);
*/


/*
    if (luaL_checkudata(L, 3, "table")) {
        fprintf(stderr, "[XX] 3 IS table!\n");
    } else {
        fprintf(stderr, "[XX] 3 IS NOT table!\n");
    }
    fflush(stderr);
*/
    /* This opens the relevent netlink socket of the relevent type */
/*
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

    //nflog_callback_register(group, &callback_handler, callback_data);

*/
    //lua_pushcfunction(L, g);


    /*
    lua_pushnumber(L, 1);
    lua_pushlightuserdata(L, group);
    lua_pushnumber(L, 3);
    lua_call(L, 3, 0);
    */

    // all ok
    lua_pushnumber(L, 32);
    return 1;

}
#if 0
    memset(buf, 0, sizeof(buf));

    /* This opens the relevent netlink socket of the relevent type */
    if ((handle = nflog_open()) == NULL){
        fprintf(stderr, "Could not get netlink handle\n");
        exit(1);
    }

    /* We tell the kernel that we want ipv4 tables not ipv6 */
    /* v6 packets are logged anyway? */
    if (nflog_bind_pf(handle, AF_INET) < 0) {
        fprintf(stderr, "Could not bind netlink handle\n");
        exit(1);
    }
    /* this causes double reports for v6
    if (nflog_bind_pf(handle, AF_INET6) < 0) {
    fprintf(stderr, "Could not bind netlink handle (6)\n");
    exit(1);
    }*/

    /* Setup groups, this binds to the group specified */
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

    /* Register the callback */
    //nflog_callback_register(group, &queue_push, (void *)queue);
    nflog_callback_register(group, callback, callback_data);

    /* Get the actual FD for the netlogger entry */
    fd = nflog_fd(handle);

    struct timeval tv;
    tv.tv_sec = 1;  /* 30 Secs Timeout */
    tv.tv_usec = 0;  // Not init'ing this can cause strange errors
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv,sizeof(struct timeval));

    /* We continually read from the loop and push the contents into
     nflog_handle_packet (which seperates one entry from the other),
     which will eventually invoke our callback (queue_push) */
    if (cleanup_handle != NULL) {
        *cleanup_handle = group;
    }
    for (;;) {
        if (mqtt != NULL) {
            mosquitto_loop(mqtt, 10, 5);
        }
        sz = recv(fd, buf, BUFSZ, 0);
        if (sz < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        } else if (sz < 0) {
            printf("Error reading from nflog socket\n");
            break;
        }
        nflog_handle_packet(handle, buf, sz);
    }


//    lua_pushnumber(
}
#endif


// function mapping
static const luaL_Reg mathlib[] = {
    {"sin", math_sin},
    {"setup_netlogger_loop", setup_netlogger_loop},
    {NULL, NULL}
};


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
