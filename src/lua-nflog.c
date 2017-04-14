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
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <libnetfilter_log/linux_nfnetlink_log.h>

#include <libnfnetlink/libnfnetlink.h>
#include <libnetfilter_log/libnetfilter_log.h>

#include <ldns/ldns.h>

#include <math.h>
#define LUA_NFLOG_NAME "nflog"
#define LUA_NFLOG_HANDLER_NAME "nflog.Handler"
#define LUA_NFLOG_EVENT_NAME "nflog.Event"
#define LUA_NFLOG_DNSPACKET_NAME "nflog.DNSPacket"
#define PI 3.1415729

static int math_sin (lua_State *L) {
    //char* a = malloc(24000);
    //(void)a;
    //lua_pushnumber(L, sin(luaL_checknumber(L, 1)));
    lua_pushnumber(L, 32);
    return 1;
}

#define BUFSZ 2097152

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

typedef struct {
    struct nfulnl_msg_packet_hdr* header;
    struct nflog_data *data;
} event_info;

static void print_nli(netlogger_info* nli) {
    printf("[Netlogger info]\n");
    printf("callback id: %d\n", nli->lua_callback_regid);
    printf("callback data id: %d\n", nli->lua_callback_data_regid);
    printf("callback fd: %d\n", nli->fd);
    printf("callback handle ptr: %p\n", nli->handle);
    printf("callback ghandle ptr: %p\n", nli->ghandle);
    printf("callback state ptr: %p\n", nli->L);
}


int callback_handler(struct nflog_g_handle *handle,
                     struct nfgenmsg *msg,
                     struct nflog_data *nfldata,
                     void *netlogger_info_ptr) {
    netlogger_info* nli = (netlogger_info*) netlogger_info_ptr;

    lua_rawgeti(nli->L, LUA_REGISTRYINDEX, nli->lua_callback_regid);
    lua_rawgeti(nli->L, LUA_REGISTRYINDEX, nli->lua_callback_data_regid);

    // create the event object and add it to the stack
    event_info* event = (event_info*) lua_newuserdata(nli->L, sizeof(event_info));
    // Make that into an actual object
    luaL_getmetatable(nli->L, LUA_NFLOG_EVENT_NAME);
    lua_setmetatable(nli->L, -2);

    event->data = nfldata;
    event->header = nflog_get_msg_packet_hdr(nfldata);

    int result = lua_pcall(nli->L, 2, 0, 0);
    if (result != 0) {
        lua_error(nli->L);
    }
    //stackdump_g(nli->L);
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
static int setup_netlogger_loop(lua_State *L) {
    int fd = -1;

    struct nflog_handle *handle = NULL;
    struct nflog_g_handle *group = NULL;

    // handle the lua arugments to this function
    int groupnum = luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    // TODO: make setter functions for the options below
    // e.g. handler:settimeout(secs, millisecs), etc.

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

    /* Get the actual FD for the netlogger entry */
    fd = nflog_fd(handle);

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv,sizeof(struct timeval));

    //stackdump_g(L);

    int cb_d = luaL_ref(L, LUA_REGISTRYINDEX);
    int cb_f = luaL_ref(L, LUA_REGISTRYINDEX);

    // Create the result and put it on the stack to return
    netlogger_info* nli = (netlogger_info*) lua_newuserdata(L, sizeof(netlogger_info));

    // Make that into an actual object
    luaL_getmetatable(L, LUA_NFLOG_HANDLER_NAME);
    lua_setmetatable(L, -2);

    nli->fd = fd;
    nli->handle = handle;
    nli->ghandle = group;
    nli->L = L;
    nli->lua_callback_data_regid = cb_d;
    nli->lua_callback_regid = cb_f;

    nflog_callback_register(group, &callback_handler, nli);

    return 1;
}


static int handler_loop_once(lua_State *L) {
    int sz;
    char buf[BUFSZ];

    netlogger_info* nli = (netlogger_info*) lua_touserdata(L, 1);
    sz = recv(nli->fd, buf, BUFSZ, 0);
    if (sz < 0 && (errno == EINTR || errno == EAGAIN)) {
        //printf("[XX] EINTR or EAGAIN\n", nli);
        return 0;
    } else if (sz < 0) {
        printf("Error reading from nflog socket: %s (%d) ((bufsize %u))\n", strerror(errno), errno, BUFSZ);
        return 0;
    }
    nflog_handle_packet(nli->handle, buf, sz);
    return 0;
}

static int handler_loop_forever(lua_State *L) {
    int sz;
    char buf[BUFSZ];

    netlogger_info* nli = (netlogger_info*) lua_touserdata(L, 1);
    for (;;) {
        sz = recv(nli->fd, buf, BUFSZ, 0);
        if (sz < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        } else if (sz < 0) {
            printf("Error reading from nflog socket\n");
            break;
        }
        nflog_handle_packet(nli->handle, buf, sz);
    }
    return 0;
}

static int handler_close(lua_State *L) {
    netlogger_info* nli = (netlogger_info*) lua_touserdata(L, 1);

    nflog_unbind_group(nli->ghandle);
    nflog_close(nli->handle);

    return 0;
}

static int event_get_from_addr(lua_State *L) {
    event_info* event = (event_info*) lua_touserdata(L, 1);

    char* data;
    size_t datalen;
    char ip_frm[INET6_ADDRSTRLEN];

    data = NULL;
    datalen = nflog_get_payload(event->data, &data);

    if (ntohs(event->header->hw_protocol) == 0x86dd) {
        inet_ntop(AF_INET6, &data[8], ip_frm, INET6_ADDRSTRLEN);
    } else {
        inet_ntop(AF_INET, &data[12], ip_frm, INET6_ADDRSTRLEN);
    }

    lua_pushstring(L, ip_frm);
    return 1;
}

static int event_get_to_addr(lua_State *L) {
    event_info* event = (event_info*) lua_touserdata(L, 1);

    char* data;
    size_t datalen;
    char ip_to[INET6_ADDRSTRLEN];

    data = NULL;
    datalen = nflog_get_payload(event->data, &data);

    if (ntohs(event->header->hw_protocol) == 0x86dd) {
        inet_ntop(AF_INET6, &data[24], ip_to, INET6_ADDRSTRLEN);
    } else {
        inet_ntop(AF_INET, &data[16], ip_to, INET6_ADDRSTRLEN);
    }

    lua_pushstring(L, ip_to);
    return 1;
}

static int event_get_payload_size(lua_State *L) {
    event_info* event = (event_info*) lua_touserdata(L, 1);
    char* data = NULL;
    size_t datalen = nflog_get_payload(event->data, &data);
    lua_pushnumber(L, datalen);
    return 1;
}

static int event_get_payload_hex(lua_State *L) {
    event_info* event = (event_info*) lua_touserdata(L, 1);
    char* data = NULL;
    size_t datalen = nflog_get_payload(event->data, &data);
    size_t i;
    char c[3];

    // create a table and put it on the stack
    lua_newtable(L);
    for (i = 0; i < datalen; i++) {
        // note: lua arrays begin at index 1
        lua_pushnumber(L, i+1);
        // should we prepend with 0x?
        sprintf(c, "%02x", (uint8_t)data[i]);
        lua_pushstring(L, c);
        lua_settable(L, -3);
    }
    return 1;
}

static int event_get_payload_dec(lua_State *L) {
    event_info* event = (event_info*) lua_touserdata(L, 1);
    char* data = NULL;
    size_t datalen = nflog_get_payload(event->data, &data);
    size_t i;
    char c[3];

    // create a table and put it on the stack
    lua_newtable(L);
    for (i = 0; i < datalen; i++) {
        // note: lua arrays begin at index 1
        lua_pushnumber(L, i+1);
        lua_pushnumber(L, (uint8_t)data[i]);
        lua_settable(L, -3);
    }
    return 1;
}

static int event_get_timestamp(lua_State *L) {
    event_info* event = (event_info*) lua_touserdata(L, 1);

    struct timeval tv;
    unsigned int timestamp;

    if (nflog_get_timestamp(event->data, &tv) > 0) {
        timestamp = tv.tv_sec;
    } else {
        timestamp = time(NULL);
    }

    lua_pushnumber(L, timestamp);
    return 1;
}

static int event_get_octet(lua_State *L) {
    char* err_info;
    event_info* event = (event_info*) lua_touserdata(L, 1);
    size_t i = lua_tonumber(L, 2);
    char* data = NULL;
    size_t datalen = nflog_get_payload(event->data, &data);
    if (i > datalen) {
        lua_pushnil(L);
        err_info = malloc(1024);
        snprintf(err_info, 1024, "octet index (%u) larger than packet size (%u)", i, datalen);
        lua_pushstring(L, err_info);
        free(err_info);
        return 2;
    }

    lua_pushnumber(L, (uint8_t)data[i]);
    return 1;
}

static int event_get_int16(lua_State *L) {
    event_info* event = (event_info*) lua_touserdata(L, 1);
    size_t i = lua_tonumber(L, 2);
    uint16_t result;
    char* data = NULL;
    size_t datalen = nflog_get_payload(event->data, &data);
    if (i+1 > datalen) {
        lua_pushnil(L);
        lua_pushstring(L, "octet index larger than packet size");
        return 2;
    }

    //printf("[XX] octet at %u: %02x\n", i, (uint8_t)data[i]);
    //printf("[XX] octet at %u: %02x\n", i+1, (uint8_t)data[i+1]);

    result = (uint8_t)data[i] * 255 + (uint8_t)data[i+1];
    lua_pushnumber(L, result);
    lua_pushnumber(L, i + 2);
    //stackdump_g(L);
    return 2;
}

static int event_get_octets(lua_State *L) {
    event_info* event = (event_info*) lua_touserdata(L, 1);
    size_t start_i = lua_tonumber(L, 2);
    size_t len = lua_tonumber(L, 3);
    size_t i;

    char* data = NULL;
    size_t datalen = nflog_get_payload(event->data, &data);
    if (start_i + len > datalen) {
        lua_pushnil(L);
        lua_pushstring(L, "octet index+range larger than packet size");
        return 2;
    }

    // create a table and put it on the stack
    lua_newtable(L);
    for (i = 0; i < len; i++) {
        // note: lua arrays begin at index 1
        lua_pushnumber(L, i+1);
        lua_pushnumber(L, (uint8_t)data[i+start_i]);
        lua_settable(L, -3);
    }
    return 1;
}

typedef struct {
  ldns_pkt* dnspacket;
} dnspacket_info;

static int event_get_payload_dns(lua_State *L) {
    event_info* event = (event_info*) lua_touserdata(L, 1);
    char* data = NULL;
    size_t datalen = nflog_get_payload(event->data, &data);
    if (datalen <= 30) {
        // TODO: error
        fprintf(stderr, "[XX] error:packet too small to be dns\n");
        lua_pushnil(L);
        lua_pushstring(L, "Data packet too small to be DNS\n");
        return 0;
    }

    dnspacket_info* dnspacket = (dnspacket_info*) lua_newuserdata(L, sizeof(dnspacket_info));


    // TODO: this function needs to be freed as well. add __gc?
    if (ldns_wire2pkt(&(dnspacket->dnspacket), &data[28], datalen-28) == LDNS_STATUS_OK) {
        // can this be done directly? do we need to copy all data?
        // can we transfer ownership and use __gc?

        //char* tmp = ldns_pkt2str(dnspacket->dnspacket);
        //printf("[XX] PACKET at %p -> %p:\n%s\n", dnspacket, dnspacket->dnspacket, tmp);
        //free(tmp);

        //printf("[XX] CREATED DNSPACKET PTR %p\n", dnspacket);

        //char* pktstr = ldns_pkt2str(dnspacket->dnspacket);
        //printf("[XX] STR: %s\n", pktstr);
        //free(pktstr);
        //ldns_pkt_free(tmp_pkt);
        // create the event object and add it to the stack
        //ldns_pkt* event = (ldns_pkt*) lua_newuserdata(nli->L, sizeof(ldns_pkt));
        // Make that into an actual object
        luaL_getmetatable(L, LUA_NFLOG_DNSPACKET_NAME);
        lua_setmetatable(L, -2);
        return 1;
    } else {
        // uninitialized packet is now on the stack, remove it
        // TODO: error
        fprintf(stderr, "[XX] ERROR PARSING DNS PACKET DATA\n");
        lua_pushnil(L);
        lua_pushstring(L, "Error parsing DNS packet\n");
    }

    return 0;
}

static int dnspacket_gc(lua_State *L) {
    dnspacket_info* event = (dnspacket_info*) lua_touserdata(L, 1);
    if (event->dnspacket != NULL) {
        ldns_pkt_free(event->dnspacket);
    }
    return 0;
}

// Library function mapping
static const luaL_Reg nflog_lib[] = {
    {"sin", math_sin},
    {"setup_netlogger_loop", setup_netlogger_loop},
    {NULL, NULL}
};

// Handler function mapping
static const luaL_Reg handler_mapping[] = {
    {"loop_forever", handler_loop_forever},
    {"loop_once", handler_loop_once},
    {"close", handler_close},
    {NULL, NULL}
};

// Event function mapping
static const luaL_Reg event_mapping[] = {
    {"get_from_addr", event_get_from_addr},
    {"get_to_addr", event_get_to_addr},
    {"get_payload_size", event_get_payload_size},
    {"get_timestamp", event_get_timestamp},
    {"get_payload_hex", event_get_payload_hex},
    {"get_payload_dec", event_get_payload_dec},
    {"get_octet", event_get_octet},
    {"get_int16", event_get_int16},
    {"get_octets", event_get_octets},
    {"get_payload_dns", event_get_payload_dns},
    {NULL, NULL}
};

//
// For some packet types, we provide additional helper classes,
// such as DNSPacket, specifically wrapping only the functionality we
// need.
//
static int dnspacket_tostring(lua_State* L) {
    dnspacket_info* dnspacket = (dnspacket_info*) lua_touserdata(L, 1);

    //printf("[XX] READING DNSPACKET PTR %p\n", dnspacket->dnspacket);

    char* pktstr = ldns_pkt2str(dnspacket->dnspacket);
    lua_pushstring(L, pktstr);
    free(pktstr);

    return 1;
}


//
// local dns packet helper functions
//
// If this returns NULL it has set nil+error on the stack
static ldns_rr* get_dns_pkt_query_rr(lua_State* L, ldns_pkt* pkt) {
    if (ldns_pkt_qdcount(pkt) == 0) {
        lua_pushnil(L);
        lua_pushstring(L, "No question in DNS packet");
        return NULL;
    }
    ldns_rr_list* questions = ldns_pkt_question(pkt);
    return ldns_rr_list_rr(questions, 0);
}

// returns 0 on error, nil+errormsg set to stack on L
static int get_dns_pkt_query_type(lua_State* L, ldns_pkt* pkt) {
    ldns_rr* qrr = get_dns_pkt_query_rr(L, pkt);
    if (qrr == NULL) {
        return 0;
    }
    return ldns_rr_get_type(qrr);
}


//
// Lua functions
//
static int dnspacket_isresponse(lua_State* L) {
    dnspacket_info* dnspacket = (dnspacket_info*) lua_touserdata(L, 1);
    lua_pushboolean(L, ldns_pkt_qr(dnspacket->dnspacket));
    return 1;
}



static int dnspacket_get_rcode(lua_State* L) {
    dnspacket_info* dnspacket = (dnspacket_info*) lua_touserdata(L, 1);
    lua_pushnumber(L, ldns_pkt_get_rcode(dnspacket->dnspacket));
    return 1;
}

static int dnspacket_get_qname(lua_State* L) {
    dnspacket_info* dnspacket = (dnspacket_info*) lua_touserdata(L, 1);
    ldns_pkt* pkt = dnspacket->dnspacket;
    ldns_rr* qrr = get_dns_pkt_query_rr(L, pkt);
    if (qrr == NULL) {
        return 2;
    }
    lua_pushstring(L, ldns_rdf2str(ldns_rr_owner(qrr)));
    return 1;
}

static int dnspacket_get_qname_second_level_only(lua_State* L) {
    dnspacket_info* dnspacket = (dnspacket_info*) lua_touserdata(L, 1);
    ldns_pkt* pkt = dnspacket->dnspacket;
    ldns_rr* qrr = get_dns_pkt_query_rr(L, pkt);
    ldns_rdf* qname;
    uint8_t label_count;

    if (qrr == NULL) {
        return 2;
    }
    label_count = (int) ldns_dname_label_count(ldns_rr_owner(qrr));
    if (label_count > 2) {
        label_count -= 2;
        qname = ldns_dname_clone_from(ldns_rr_owner(qrr), label_count);
    } else {
        qname = ldns_rdf_clone(ldns_rr_owner(qrr));
    }
    lua_pushstring(L, ldns_rdf2str(qname));
    ldns_rdf_deep_free(qname);
    return 1;
}


static int dnspacket_get_qtype(lua_State* L) {
    dnspacket_info* dnspacket = (dnspacket_info*) lua_touserdata(L, 1);
    ldns_pkt* pkt = dnspacket->dnspacket;
    ldns_rr* qrr = get_dns_pkt_query_rr(L, pkt);
    if (qrr == NULL) {
        return 2;
    }
    int qtype = get_dns_pkt_query_type(L, pkt);
    if (qtype == 0) {
        return 2;
    } else {
        lua_pushnumber(L, qtype);
        return 1;
    }
}

static int dnspacket_get_answer_address_strings(lua_State* L) {
    dnspacket_info* dnspacket = (dnspacket_info*) lua_touserdata(L, 1);
    ldns_pkt* pkt = dnspacket->dnspacket;

    int i, j;
    ldns_rr* cur_rr;

    ldns_rr* qrr = get_dns_pkt_query_rr(L, pkt);
    if (qrr == NULL) {
        return 2;
    }
    int qtype = get_dns_pkt_query_type(L, pkt);
    if (qtype == 0) {
        return 2;
    }
    ldns_rr_list* answers = ldns_pkt_answer(pkt);


    // create a table and put it on the stack
    lua_newtable(L);
    // note: lua arrays begin at index 1
    j = 1;
    for (i = 0; i < ldns_rr_list_rr_count(answers); i++) {
        cur_rr = ldns_rr_list_rr(answers, i);
        if (ldns_rr_get_type(cur_rr) == qtype) {
            lua_pushnumber(L, j);
            lua_pushstring(L, ldns_rdf2str(ldns_rr_rdf(cur_rr, 0)));
            lua_settable(L, -3);
            j++;
        }
    }
    return 1;
}

// necessary functions:
// -get_rcode()
// -get_qname()
// -get_qtype()
// -get_ancount()
// -get_answer_rdatas(type)
static const luaL_Reg dnspacket_mapping[] = {
    // some general functions
    {"tostring", dnspacket_tostring},
    {"is_response", dnspacket_isresponse},
    {"get_rcode", dnspacket_get_qname},
    {"get_qname", dnspacket_get_qname},
    {"get_qname_second_level_only", dnspacket_get_qname_second_level_only},
    {"get_qtype", dnspacket_get_qtype},
    // some highly specific functions for SPIN
    {"get_answer_address_strings", dnspacket_get_answer_address_strings},
    {"__gc", dnspacket_gc},
    {NULL, NULL}
};

/*
** Netfilter-log library initialization
*/
LUALIB_API int luaopen_lnflog (lua_State *L) {

    // register the handler class
    luaL_newmetatable(L, LUA_NFLOG_HANDLER_NAME); //leaves new metatable on the stack
    lua_pushvalue(L, -1); // there are two 'copies' of the metatable on the stack
    lua_setfield(L, -2, "__index"); // pop one of those copies and assign it to
                                    // __index field od the 1st metatable
    luaL_register(L, NULL, handler_mapping); // register functions in the metatable

    // register the event class
    luaL_newmetatable(L, LUA_NFLOG_EVENT_NAME); //leaves new metatable on the stack
    lua_pushvalue(L, -1); // there are two 'copies' of the metatable on the stack
    lua_setfield(L, -2, "__index"); // pop one of those copies and assign it to
                                    // __index field od the 1st metatable
    luaL_register(L, NULL, event_mapping); // register functions in the metatable

    luaL_newmetatable(L, LUA_NFLOG_DNSPACKET_NAME); //leaves new metatable on the stack
    lua_pushvalue(L, -1); // there are two 'copies' of the metatable on the stack
    lua_setfield(L, -2, "__index"); // pop one of those copies and assign it to
                                    // __index field od the 1st metatable
    luaL_register(L, NULL, dnspacket_mapping); // register functions in the metatable

    // register the library itself
    luaL_register(L, LUA_NFLOG_NAME, nflog_lib);

    lua_pushnumber(L, PI);
    lua_setfield(L, -2, "pi");
    lua_pushnumber(L, HUGE_VAL);
    lua_setfield(L, -2, "huge");

    // any additional initialization code goes here

    return 1;
}
