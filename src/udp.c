/*=========================================================================*\
* UDP object 
* LuaSocket toolkit
*
* RCS ID: $Id$
\*=========================================================================*/
#include <string.h> 

#include <lua.h>
#include <lauxlib.h>

#include "luasocket.h"

#include "auxiliar.h"
#include "socket.h"
#include "inet.h"
#include "udp.h"

/*=========================================================================*\
* Internal function prototypes
\*=========================================================================*/
static int global_create(lua_State *L);
static int meth_send(lua_State *L);
static int meth_sendto(lua_State *L);
static int meth_receive(lua_State *L);
static int meth_receivefrom(lua_State *L);
static int meth_getsockname(lua_State *L);
static int meth_getpeername(lua_State *L);
static int meth_setsockname(lua_State *L);
static int meth_setpeername(lua_State *L);
static int meth_close(lua_State *L);
static int meth_shutdown(lua_State *L);
static int meth_setoption(lua_State *L);
static int meth_settimeout(lua_State *L);
static int meth_fd(lua_State *L);
static int meth_dirty(lua_State *L);
static int opt_dontroute(lua_State *L);
static int opt_broadcast(lua_State *L);
static int opt_reuseaddr(lua_State *L);
static int opt_ip_multicast_ttl(lua_State *L);
static int opt_ip_multicast_loop(lua_State *L);
static int opt_ip_add_membership(lua_State *L);
static int opt_ip_drop_membersip(lua_State *L);

/* udp object methods */
static luaL_reg udp[] = {
    {"setpeername", meth_setpeername},
    {"setsockname", meth_setsockname},
    {"getsockname", meth_getsockname},
    {"getpeername", meth_getpeername},
    {"send",        meth_send},
    {"sendto",      meth_sendto},
    {"receive",     meth_receive},
    {"receivefrom", meth_receivefrom},
    {"settimeout",  meth_settimeout},
    {"close",       meth_close},
    {"shutdown",    meth_shutdown},
    {"setoption",   meth_setoption},
    {"__gc",        meth_close},
    {"fd",          meth_fd},
    {"dirty",       meth_dirty},
    {NULL,          NULL}
};

/* socket options */
static luaL_reg opt[] = {
    {"dontroute",          opt_dontroute},
    {"broadcast",          opt_broadcast},
    {"reuseaddr",          opt_reuseaddr},
    {"ip-multicast-ttl",   opt_ip_multicast_ttl},
    {"ip-multicast-loop",  opt_ip_multicast_loop},
    {"ip-add-membership",  opt_ip_add_membership},
    {"ip-drop-membership", opt_ip_drop_membersip},
    {NULL,          NULL}
};

/* functions in library namespace */
static luaL_reg func[] = {
    {"udp", global_create},
    {NULL, NULL}
};

/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
void udp_open(lua_State *L)
{
    /* create classes */
    aux_newclass(L, "udp{connected}", udp);
    aux_newclass(L, "udp{unconnected}", udp);
    /* create class groups */
    aux_add2group(L, "udp{connected}",   "udp{any}");
    aux_add2group(L, "udp{unconnected}", "udp{any}");
    aux_add2group(L, "udp{connected}",   "select{able}");
    aux_add2group(L, "udp{unconnected}", "select{able}");
    /* define library functions */
    luaL_openlib(L, LUASOCKET_LIBNAME, func, 0); 
    lua_pop(L, 1);
}

/*=========================================================================*\
* Lua methods
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Send data through connected udp socket
\*-------------------------------------------------------------------------*/
static int meth_send(lua_State *L)
{
    p_udp udp = (p_udp) aux_checkclass(L, "udp{connected}", 1);
    p_tm tm = &udp->tm;
    size_t count, sent = 0;
    int err;
    const char *data = luaL_checklstring(L, 2, &count);
    tm_markstart(tm);
    err = sock_send(&udp->sock, data, count, &sent, tm_get(tm));
    if (err == IO_DONE) lua_pushnumber(L, sent);
    else lua_pushnil(L);
    /* a 'closed' error on an unconnected means the target address was not
     * accepted by the transport layer */
    io_pusherror(L, err == IO_CLOSED ? IO_REFUSED : err);
    return 2;
}

/*-------------------------------------------------------------------------*\
* Send data through unconnected udp socket
\*-------------------------------------------------------------------------*/
static int meth_sendto(lua_State *L)
{
    p_udp udp = (p_udp) aux_checkclass(L, "udp{unconnected}", 1);
    size_t count, sent = 0;
    const char *data = luaL_checklstring(L, 2, &count);
    const char *ip = luaL_checkstring(L, 3);
    unsigned short port = (unsigned short) luaL_checknumber(L, 4);
    p_tm tm = &udp->tm;
    struct sockaddr_in addr;
    int err;
    memset(&addr, 0, sizeof(addr));
    if (!inet_aton(ip, &addr.sin_addr)) 
        luaL_argerror(L, 3, "invalid ip address");
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    tm_markstart(tm);
    err = sock_sendto(&udp->sock, data, count, &sent, 
            (SA *) &addr, sizeof(addr), tm_get(tm));
    if (err == IO_DONE) lua_pushnumber(L, sent);
    else lua_pushnil(L);
    /* a 'closed' error on an unconnected means the target address was not
     * accepted by the transport layer */
    io_pusherror(L, err == IO_CLOSED ? IO_REFUSED : err);
    return 2;
}

/*-------------------------------------------------------------------------*\
* Receives data from a UDP socket
\*-------------------------------------------------------------------------*/
static int meth_receive(lua_State *L)
{
    p_udp udp = (p_udp) aux_checkgroup(L, "udp{any}", 1);
    char buffer[UDP_DATAGRAMSIZE];
    size_t got, count = (size_t) luaL_optnumber(L, 2, sizeof(buffer));
    int err;
    p_tm tm = &udp->tm;
    count = MIN(count, sizeof(buffer));
    tm_markstart(tm);
    err = sock_recv(&udp->sock, buffer, count, &got, tm_get(tm));
    if (err == IO_DONE) lua_pushlstring(L, buffer, got);
    else lua_pushnil(L);
    io_pusherror(L, err);
    return 2;
}

/*-------------------------------------------------------------------------*\
* Receives data and sender from a UDP socket
\*-------------------------------------------------------------------------*/
static int meth_receivefrom(lua_State *L)
{
    p_udp udp = (p_udp) aux_checkclass(L, "udp{unconnected}", 1);
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    char buffer[UDP_DATAGRAMSIZE];
    size_t got, count = (size_t) luaL_optnumber(L, 2, sizeof(buffer));
    int err;
    p_tm tm = &udp->tm;
    tm_markstart(tm);
    count = MIN(count, sizeof(buffer));
    err = sock_recvfrom(&udp->sock, buffer, count, &got, 
            (SA *) &addr, &addr_len, tm_get(tm));
    if (err == IO_DONE) {
        lua_pushlstring(L, buffer, got);
        lua_pushstring(L, inet_ntoa(addr.sin_addr));
        lua_pushnumber(L, ntohs(addr.sin_port));
        return 3;
    } else {
        lua_pushnil(L);
        io_pusherror(L, err);
        return 2;
    }
}

/*-------------------------------------------------------------------------*\
* Select support methods
\*-------------------------------------------------------------------------*/
static int meth_fd(lua_State *L)
{
    p_udp udp = (p_udp) aux_checkgroup(L, "udp{any}", 1);
    lua_pushnumber(L, udp->sock);
    return 1;
}

static int meth_dirty(lua_State *L)
{
    p_udp udp = (p_udp) aux_checkgroup(L, "udp{any}", 1);
    (void) udp;
    lua_pushboolean(L, 0);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Just call inet methods
\*-------------------------------------------------------------------------*/
static int meth_getpeername(lua_State *L)
{
    p_udp udp = (p_udp) aux_checkclass(L, "udp{connected}", 1);
    return inet_meth_getpeername(L, &udp->sock);
}

static int meth_getsockname(lua_State *L)
{
    p_udp udp = (p_udp) aux_checkgroup(L, "udp{any}", 1);
    return inet_meth_getsockname(L, &udp->sock);
}

/*-------------------------------------------------------------------------*\
* Option handlers
\*-------------------------------------------------------------------------*/
static int meth_setoption(lua_State *L)
{
    return aux_meth_setoption(L, opt);
}

static int opt_boolean(lua_State *L, int level, int name)
{
    p_udp udp = (p_udp) aux_checkgroup(L, "udp{any}", 1);
    int val = aux_checkboolean(L, 2);
    if (setsockopt(udp->sock, level, name, (char *) &val, sizeof(val)) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "setsockopt failed");
        return 2;
    }
    lua_pushnumber(L, 1);
    return 1;
}

static int opt_dontroute(lua_State *L)
{
    return opt_boolean(L, SOL_SOCKET, SO_DONTROUTE);
}

static int opt_reuseaddr(lua_State *L)
{
    return opt_boolean(L, SOL_SOCKET, SO_REUSEADDR); 
}

static int opt_broadcast(lua_State *L)
{
    return opt_boolean(L, SOL_SOCKET, SO_BROADCAST);
}

static int opt_ip_multicast_loop(lua_State *L)
{
    return opt_boolean(L, IPPROTO_IP, IP_MULTICAST_LOOP);
}

static int opt_ip_multicast_ttl(lua_State *L)
{
    p_udp udp = (p_udp) aux_checkgroup(L, "udp{any}", 1);
    int val = (int) luaL_checknumber(L, 2);
    if (setsockopt(udp->sock, IPPROTO_IP, IP_MULTICAST_TTL, 
                (char *) &val, sizeof(val)) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "setsockopt failed");
        return 2;
    }
    lua_pushnumber(L, 1);
    return 1;
}

static int opt_membership(lua_State *L, int level, int name)
{
    p_udp udp = (p_udp) aux_checkgroup(L, "udp{any}", 1);
    struct ip_mreq val;
    if (!lua_istable(L, 2))
        luaL_typerror(L, 2, lua_typename(L, LUA_TTABLE));
    lua_pushstring(L, "multiaddr");
    lua_gettable(L, 2);
    if (!lua_isstring(L, -1)) luaL_argerror(L, 2, "invalid 'group' field");
    if (!inet_aton(lua_tostring(L, -1), &val.imr_multiaddr)) 
        luaL_argerror(L, 3, "invalid 'multiaddr' ip address");
    lua_pushstring(L, "interface");
    lua_gettable(L, 2);
    if (!lua_isstring(L, -1)) luaL_argerror(L, 2, "invalid 'interface' field");
    val.imr_interface.s_addr = htonl(INADDR_ANY);
    if (strcmp(lua_tostring(L, -1), "*") &&
            !inet_aton(lua_tostring(L, -1), &val.imr_interface)) 
        luaL_argerror(L, 3, "invalid 'interface' ip address");
    if (setsockopt(udp->sock, level, name, (char *) &val, sizeof(val)) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "setsockopt failed");
        return 2;
    }
    lua_pushnumber(L, 1);
    return 1;
}

static int opt_ip_add_membership(lua_State *L)
{
    return opt_membership(L, IPPROTO_IP, IP_ADD_MEMBERSHIP);
}

static int opt_ip_drop_membersip(lua_State *L)
{
    return opt_membership(L, IPPROTO_IP, IP_DROP_MEMBERSHIP);
}

/*-------------------------------------------------------------------------*\
* Just call tm methods
\*-------------------------------------------------------------------------*/
static int meth_settimeout(lua_State *L)
{
    p_udp udp = (p_udp) aux_checkgroup(L, "udp{any}", 1);
    return tm_meth_settimeout(L, &udp->tm);
}

/*-------------------------------------------------------------------------*\
* Turns a master udp object into a client object.
\*-------------------------------------------------------------------------*/
static int meth_setpeername(lua_State *L)
{
    p_udp udp = (p_udp) aux_checkclass(L, "udp{unconnected}", 1);
    p_tm tm = &udp->tm;
    const char *address =  luaL_checkstring(L, 2);
    int connecting = strcmp(address, "*");
    unsigned short port = connecting ? 
        (unsigned short) luaL_checknumber(L, 3) : 
        (unsigned short) luaL_optnumber(L, 3, 0);
    const char *err = inet_tryconnect(&udp->sock, tm, address, port);
    if (err) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    /* change class to connected or unconnected depending on address */
    if (connecting) aux_setclass(L, "udp{connected}", 1);
    else aux_setclass(L, "udp{unconnected}", 1);
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Closes socket used by object 
\*-------------------------------------------------------------------------*/
static int meth_close(lua_State *L)
{
    p_udp udp = (p_udp) aux_checkgroup(L, "udp{any}", 1);
    sock_destroy(&udp->sock);
    return 0;
}

/*-------------------------------------------------------------------------*\
* Shuts the connection down partially
\*-------------------------------------------------------------------------*/
static int meth_shutdown(lua_State *L)
{
    p_udp udp = (p_udp) aux_checkgroup(L, "udp{any}", 1);
    const char *how = luaL_optstring(L, 2, "both");
    switch (how[0]) {
        case 'b':
            if (strcmp(how, "both")) goto error;
            sock_shutdown(&udp->sock, 2);
            break;
        case 's':
            if (strcmp(how, "send")) goto error;
            sock_shutdown(&udp->sock, 1);
            break;
        case 'r':
            if (strcmp(how, "receive")) goto error;
            sock_shutdown(&udp->sock, 0);
            break;
    }
    return 0;
error:
    luaL_argerror(L, 2, "invalid shutdown method");
    return 0;
}

/*-------------------------------------------------------------------------*\
* Turns a master object into a server object
\*-------------------------------------------------------------------------*/
static int meth_setsockname(lua_State *L)
{
    p_udp udp = (p_udp) aux_checkclass(L, "udp{unconnected}", 1);
    const char *address =  luaL_checkstring(L, 2);
    unsigned short port = (unsigned short) luaL_checknumber(L, 3);
    const char *err = inet_trybind(&udp->sock, address, port, -1);
    if (err) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    lua_pushnumber(L, 1);
    return 1;
}

/*=========================================================================*\
* Library functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Creates a master udp object 
\*-------------------------------------------------------------------------*/
static int global_create(lua_State *L)
{
    t_sock sock;
    const char *err = inet_trycreate(&sock, SOCK_DGRAM);
    /* try to allocate a system socket */
    if (!err) { 
        /* allocate tcp object */
        p_udp udp = (p_udp) lua_newuserdata(L, sizeof(t_udp));
        udp->sock = sock;
        /* set its type as master object */
        aux_setclass(L, "udp{unconnected}", -1);
        /* initialize remaining structure fields */
        tm_init(&udp->tm, -1, -1);
        return 1;
    } else {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
}
