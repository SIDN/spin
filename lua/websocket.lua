-- this is the squished version of the websockets library from
-- https://github.com/lipp/lua-websockets
-- License: MIT

package.preload['websocket.sync'] = (function (...)
local frame = require'websocket.frame'
local handshake = require'websocket.handshake'
local tools = require'websocket.tools'
local ssl = require'ssl'
local tinsert = table.insert
local tconcat = table.concat

local receive = function(self)
  if self.state ~= 'OPEN' and not self.is_closing then
    return nil,nil,false,1006,'wrong state'
  end
  local first_opcode
  local frames
  local bytes = 3
  local encoded = ''
  local clean = function(was_clean,code,reason)
    self.state = 'CLOSED'
    self:sock_close()
    if self.on_close then
      self:on_close()
    end
    return nil,nil,was_clean,code,reason or 'closed'
  end
  while true do
    local chunk,err = self:sock_receive(bytes)
    if err then
      return clean(false,1006,err)
    end
    encoded = encoded..chunk
    local decoded,fin,opcode,_,masked = frame.decode(encoded)
    if not self.is_server and masked then
      return clean(false,1006,'Websocket receive failed: frame was not masked')
    end
    if decoded then
      if opcode == frame.CLOSE then
        if not self.is_closing then
          local code,reason = frame.decode_close(decoded)
          -- echo code
          local msg = frame.encode_close(code)
          local encoded = frame.encode(msg,frame.CLOSE,not self.is_server)
          local n,err = self:sock_send(encoded)
          if n == #encoded then
            return clean(true,code,reason)
          else
            return clean(false,code,err)
          end
        else
          return decoded,opcode
        end
      end
      if not first_opcode then
        first_opcode = opcode
      end
      if not fin then
        if not frames then
          frames = {}
        elseif opcode ~= frame.CONTINUATION then
          return clean(false,1002,'protocol error')
        end
        bytes = 3
        encoded = ''
        tinsert(frames,decoded)
      elseif not frames then
        return decoded,first_opcode
      else
        tinsert(frames,decoded)
        return tconcat(frames),first_opcode
      end
    else
      assert(type(fin) == 'number' and fin > 0)
      bytes = fin
    end
  end
  assert(false,'never reach here')
end

local send = function(self,data,opcode)
  if self.state ~= 'OPEN' then
    return nil,false,1006,'wrong state'
  end
  local encoded = frame.encode(data,opcode or frame.TEXT,not self.is_server)
  local n,err = self:sock_send(encoded)
  if n ~= #encoded then
    return nil,self:close(1006,err)
  end
  return true
end

local close = function(self,code,reason)
  if self.state ~= 'OPEN' then
    return false,1006,'wrong state'
  end
  if self.state == 'CLOSED' then
    return false,1006,'wrong state'
  end
  local msg = frame.encode_close(code or 1000,reason)
  local encoded = frame.encode(msg,frame.CLOSE,not self.is_server)
  local n,err = self:sock_send(encoded)
  local was_clean = false
  local code = 1005
  local reason = ''
  if n == #encoded then
    self.is_closing = true
    local rmsg,opcode = self:receive()
    if rmsg and opcode == frame.CLOSE then
      code,reason = frame.decode_close(rmsg)
      was_clean = true
    end
  else
    reason = err
  end
  self:sock_close()
  if self.on_close then
    self:on_close()
  end
  self.state = 'CLOSED'
  return was_clean,code,reason or ''
end

local connect = function(self,ws_url,ws_protocol,ssl_params)
  if self.state ~= 'CLOSED' then
    return nil,'wrong state',nil
  end
  local protocol,host,port,uri = tools.parse_url(ws_url)
  -- Preconnect (for SSL if needed)
  local _,err = self:sock_connect(host,port)
  if err then
    return nil,err,nil
  end
  if protocol == 'wss' then
    self.sock = ssl.wrap(self.sock, ssl_params)
    self.sock:dohandshake()
  elseif protocol ~= "ws" then
    return nil, 'bad protocol'
  end
  local ws_protocols_tbl = {''}
  if type(ws_protocol) == 'string' then
      ws_protocols_tbl = {ws_protocol}
  elseif type(ws_protocol) == 'table' then
      ws_protocols_tbl = ws_protocol
  end
  local key = tools.generate_key()
  local req = handshake.upgrade_request
  {
    key = key,
    host = host,
    port = port,
    protocols = ws_protocols_tbl,
    uri = uri
  }
  local n,err = self:sock_send(req)
  if n ~= #req then
    return nil,err,nil
  end
  local resp = {}
  repeat
    local line,err = self:sock_receive('*l')
    resp[#resp+1] = line
    if err then
      return nil,err,nil
    end
  until line == ''
  local response = table.concat(resp,'\r\n')
  local headers = handshake.http_headers(response)
  local expected_accept = handshake.sec_websocket_accept(key)
  if headers['sec-websocket-accept'] ~= expected_accept then
    local msg = 'Websocket Handshake failed: Invalid Sec-Websocket-Accept (expected %s got %s)'
    return nil,msg:format(expected_accept,headers['sec-websocket-accept'] or 'nil'),headers
  end
  self.state = 'OPEN'
  return true,headers['sec-websocket-protocol'],headers
end

local extend = function(obj)
  assert(obj.sock_send)
  assert(obj.sock_receive)
  assert(obj.sock_close)

  assert(obj.is_closing == nil)
  assert(obj.receive    == nil)
  assert(obj.send       == nil)
  assert(obj.close      == nil)
  assert(obj.connect    == nil)

  if not obj.is_server then
    assert(obj.sock_connect)
  end

  if not obj.state then
    obj.state = 'CLOSED'
  end

  obj.receive = receive
  obj.send = send
  obj.close = close
  obj.connect = connect

  return obj
end

return {
  extend = extend
}
 end)
package.preload['websocket.client'] = (function (...)
return setmetatable({},{__index = function(self, name)
  if name == 'new' then name = 'sync' end
  local backend = require("websocket.client_" .. name)
  self[name] = backend
  if name == 'sync' then self.new = backend end
  return backend
end})
 end)
package.preload['websocket.client_copas'] = (function (...)
local socket = require'socket'
local sync = require'websocket.sync'
local tools = require'websocket.tools'

local new = function(ws)
  ws = ws or {}
  local copas = require'copas'
  
  local self = {}
  
  self.sock_connect = function(self,host,port)
    self.sock = socket.tcp()
    if ws.timeout ~= nil then
      self.sock:settimeout(ws.timeout)
    end
    local _,err = copas.connect(self.sock,host,port)
    if err and err ~= 'already connected' then
      self.sock:close()
      return nil,err
    end
  end
  
  self.sock_send = function(self,...)
    return copas.send(self.sock,...)
  end
  
  self.sock_receive = function(self,...)
    return copas.receive(self.sock,...)
  end
  
  self.sock_close = function(self)
    self.sock:shutdown()
    self.sock:close()
  end
  
  self = sync.extend(self)
  return self
end

return new
 end)
package.preload['websocket.server'] = (function (...)
return setmetatable({},{__index = function(self, name)
  local backend = require("websocket.server_" .. name)
  self[name] = backend
  return backend
end})
 end)
package.preload['websocket.server_copas'] = (function (...)

local socket = require'socket'
local copas = require'copas'
local tools = require'websocket.tools'
local frame = require'websocket.frame'
local handshake = require'websocket.handshake'
local sync = require'websocket.sync'
local tconcat = table.concat
local tinsert = table.insert

local clients = {}

local client = function(sock,protocol)
  local copas = require'copas'
  
  local self = {}
  
  self.state = 'OPEN'
  self.is_server = true
  
  self.sock_send = function(self,...)
    return copas.send(sock,...)
  end
  
  self.sock_receive = function(self,...)
    return copas.receive(sock,...)
  end
  
  self.sock_close = function(self)
    sock:shutdown()
    sock:close()
  end
  
  self = sync.extend(self)
  
  self.on_close = function(self)
    clients[protocol][self] = nil
  end
  
  self.broadcast = function(self,...)
    for client in pairs(clients[protocol]) do
      if client ~= self then
        client:send(...)
      end
    end
    self:send(...)
  end
  
  return self
end

local listen = function(opts)
  
  local copas = require'copas'
  assert(opts and (opts.protocols or opts.default))
  local on_error = opts.on_error or function(s) print(s) end
  local listener,err = socket.bind(opts.interface or '*',opts.port or 80)
  if err then
    error(err)
  end
  local protocols = {}
  if opts.protocols then
    for protocol in pairs(opts.protocols) do
      clients[protocol] = {}
      tinsert(protocols,protocol)
    end
  end
  -- true is the 'magic' index for the default handler
  clients[true] = {}
  copas.addserver(
    listener,
    function(sock)
      local request = {}
      repeat
        -- no timeout used, so should either return with line or err
        local line,err = copas.receive(sock,'*l')
        if line then
          request[#request+1] = line
        else
          sock:close()
          if on_error then
            on_error('invalid request')
          end
          return
        end
      until line == ''
      local upgrade_request = tconcat(request,'\r\n')
      local response,protocol = handshake.accept_upgrade(upgrade_request,protocols)
      if not response then
        copas.send(sock,protocol)
        sock:close()
        if on_error then
          on_error('invalid request')
        end
        return
      end
      copas.send(sock,response)
      local handler
      local new_client
      local protocol_index
      if protocol and opts.protocols[protocol] then
        protocol_index = protocol
        handler = opts.protocols[protocol]
      elseif opts.default then
        -- true is the 'magic' index for the default handler
        protocol_index = true
        handler = opts.default
      else
        sock:close()
        if on_error then
          on_error('bad protocol')
        end
        return
      end
      new_client = client(sock,protocol_index)
      clients[protocol_index][new_client] = true
      handler(new_client)
      -- this is a dirty trick for preventing
      -- copas from automatically and prematurely closing
      -- the socket
      while new_client.state ~= 'CLOSED' do
        local dummy = {
          send = function() end,
          close = function() end
        }
        copas.send(dummy)
      end
    end)
  local self = {}
  self.close = function(_,keep_clients)
    copas.removeserver(listener)
    listener = nil
    if not keep_clients then
      for protocol,clients in pairs(clients) do
        for client in pairs(clients) do
          client:close()
        end
      end
    end
  end
  return self
end

return {
  listen = listen
}
 end)
package.preload['websocket.handshake'] = (function (...)
local sha1 = require'websocket.tools'.sha1
local base64 = require'websocket.tools'.base64
local tinsert = table.insert

local guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

local sec_websocket_accept = function(sec_websocket_key)
  local a = sec_websocket_key..guid
  local sha1 = sha1(a)
  assert((#sha1 % 2) == 0)
  return base64.encode(sha1)
end

local http_headers = function(request)
  local headers = {}
  if not request:match('.*HTTP/1%.1') then
    return headers
  end
  request = request:match('[^\r\n]+\r\n(.*)')
  local empty_line
  for line in request:gmatch('[^\r\n]*\r\n') do
    local name,val = line:match('([^%s]+)%s*:%s*([^\r\n]+)')
    if name and val then
      name = name:lower()
      if not name:match('sec%-websocket') then
        val = val:lower()
      end
      if not headers[name] then
        headers[name] = val
      else
        headers[name] = headers[name]..','..val
      end
    elseif line == '\r\n' then
      empty_line = true
    else
      assert(false,line..'('..#line..')')
    end
  end
  return headers,request:match('\r\n\r\n(.*)')
end

local upgrade_request = function(req)
  local format = string.format
  local lines = {
    format('GET %s HTTP/1.1',req.uri or ''),
    format('Host: %s',req.host),
    'Upgrade: websocket',
    'Connection: Upgrade',
    format('Sec-WebSocket-Key: %s',req.key),
    format('Sec-WebSocket-Protocol: %s',table.concat(req.protocols,', ')),
    'Sec-WebSocket-Version: 13',
  }
  if req.origin then
    tinsert(lines,string.format('Origin: %s',req.origin))
  end
  if req.port and req.port ~= 80 then
    lines[2] = format('Host: %s:%d',req.host,req.port)
  end
  tinsert(lines,'\r\n')
  return table.concat(lines,'\r\n')
end

local accept_upgrade = function(request,protocols)
  local headers = http_headers(request)
  if headers['upgrade'] ~= 'websocket' or
  not headers['connection'] or
  not headers['connection']:match('upgrade') or
  headers['sec-websocket-key'] == nil or
  headers['sec-websocket-version'] ~= '13' then
    return nil,'HTTP/1.1 400 Bad Request\r\n\r\n'
  end
  local prot
  if headers['sec-websocket-protocol'] then
    for protocol in headers['sec-websocket-protocol']:gmatch('([^,%s]+)%s?,?') do
      for _,supported in ipairs(protocols) do
        if supported == protocol then
          prot = protocol
          break
        end
      end
      if prot then
        break
      end
    end
  end
  local lines = {
    'HTTP/1.1 101 Switching Protocols',
    'Upgrade: websocket',
    'Connection: '..headers['connection'],
    string.format('Sec-WebSocket-Accept: %s',sec_websocket_accept(headers['sec-websocket-key'])),
  }
  if prot then
    tinsert(lines,string.format('Sec-WebSocket-Protocol: %s',prot))
  end
  tinsert(lines,'\r\n')
  return table.concat(lines,'\r\n'),prot
end

return {
  sec_websocket_accept = sec_websocket_accept,
  http_headers = http_headers,
  accept_upgrade = accept_upgrade,
  upgrade_request = upgrade_request,
}
 end)
package.preload['websocket.tools'] = (function (...)
local bit = require'websocket.bit'
local mime = require'mime'
local rol = bit.rol
local bxor = bit.bxor
local bor = bit.bor
local band = bit.band
local bnot = bit.bnot
local lshift = bit.lshift
local rshift = bit.rshift
local sunpack = string.unpack
local srep = string.rep
local schar = string.char
local tremove = table.remove
local tinsert = table.insert
local tconcat = table.concat
local mrandom = math.random

local read_n_bytes = function(str, pos, n)
  pos = pos or 1
  return pos+n, string.byte(str, pos, pos + n - 1)
end

local read_int8 = function(str, pos)
  return read_n_bytes(str, pos, 1)
end

local read_int16 = function(str, pos)
  local new_pos,a,b = read_n_bytes(str, pos, 2)
  return new_pos, lshift(a, 8) + b
end

local read_int32 = function(str, pos)
  local new_pos,a,b,c,d = read_n_bytes(str, pos, 4)
  return new_pos,
  lshift(a, 24) +
  lshift(b, 16) +
  lshift(c, 8 ) +
  d
end

local pack_bytes = string.char

local write_int8 = pack_bytes

local write_int16 = function(v)
  return pack_bytes(rshift(v, 8), band(v, 0xFF))
end

local write_int32 = function(v)
  return pack_bytes(
    band(rshift(v, 24), 0xFF),
    band(rshift(v, 16), 0xFF),
    band(rshift(v,  8), 0xFF),
    band(v, 0xFF)
  )
end

-- used for generate key random ops
math.randomseed(os.time())

-- SHA1 hashing from luacrypto, if available
local sha1_crypto
local done,crypto = pcall(require,'crypto')
if done then
  sha1_crypto = function(msg)
    return crypto.digest('sha1',msg,true)
  end
end

-- from wiki article, not particularly clever impl
local sha1_wiki = function(msg)
  local h0 = 0x67452301
  local h1 = 0xEFCDAB89
  local h2 = 0x98BADCFE
  local h3 = 0x10325476
  local h4 = 0xC3D2E1F0

  local bits = #msg * 8
  -- append b10000000
  msg = msg..schar(0x80)

  -- 64 bit length will be appended
  local bytes = #msg + 8

  -- 512 bit append stuff
  local fill_bytes = 64 - (bytes % 64)
  if fill_bytes ~= 64 then
    msg = msg..srep(schar(0),fill_bytes)
  end

  -- append 64 big endian length
  local high = math.floor(bits/2^32)
  local low = bits - high*2^32
  msg = msg..write_int32(high)..write_int32(low)

  assert(#msg % 64 == 0,#msg % 64)

  for j=1,#msg,64 do
    local chunk = msg:sub(j,j+63)
    assert(#chunk==64,#chunk)
    local words = {}
    local next = 1
    local word
    repeat
      next,word = read_int32(chunk, next)
      tinsert(words, word)
    until next > 64
    assert(#words==16)
    for i=17,80 do
      words[i] = bxor(words[i-3],words[i-8],words[i-14],words[i-16])
      words[i] = rol(words[i],1)
    end
    local a = h0
    local b = h1
    local c = h2
    local d = h3
    local e = h4

    for i=1,80 do
      local k,f
      if i > 0 and i < 21 then
        f = bor(band(b,c),band(bnot(b),d))
        k = 0x5A827999
      elseif i > 20 and i < 41 then
        f = bxor(b,c,d)
        k = 0x6ED9EBA1
      elseif i > 40 and i < 61 then
        f = bor(band(b,c),band(b,d),band(c,d))
        k = 0x8F1BBCDC
      elseif i > 60 and i < 81 then
        f = bxor(b,c,d)
        k = 0xCA62C1D6
      end

      local temp = rol(a,5) + f + e + k + words[i]
      e = d
      d = c
      c = rol(b,30)
      b = a
      a = temp
    end

    h0 = h0 + a
    h1 = h1 + b
    h2 = h2 + c
    h3 = h3 + d
    h4 = h4 + e

  end

  -- necessary on sizeof(int) == 32 machines
  h0 = band(h0,0xffffffff)
  h1 = band(h1,0xffffffff)
  h2 = band(h2,0xffffffff)
  h3 = band(h3,0xffffffff)
  h4 = band(h4,0xffffffff)

  return write_int32(h0)..write_int32(h1)..write_int32(h2)..write_int32(h3)..write_int32(h4)
end

local base64_encode = function(data)
  return (mime.b64(data))
end

local DEFAULT_PORTS = {ws = 80, wss = 443}

local parse_url = function(url)
  local protocol, address, uri = url:match('^(%w+)://([^/]+)(.*)$')
  if not protocol then error('Invalid URL:'..url) end
  protocol = protocol:lower()
  local host, port = address:match("^(.+):(%d+)$")
  if not host then
    host = address
    port = DEFAULT_PORTS[protocol]
  end
  if not uri or uri == '' then uri = '/' end
  return protocol, host, tonumber(port), uri
end

local generate_key = function()
  local r1 = mrandom(0,0xfffffff)
  local r2 = mrandom(0,0xfffffff)
  local r3 = mrandom(0,0xfffffff)
  local r4 = mrandom(0,0xfffffff)
  local key = write_int32(r1)..write_int32(r2)..write_int32(r3)..write_int32(r4)
  assert(#key==16,#key)
  return base64_encode(key)
end

return {
  sha1 = sha1_crypto or sha1_wiki,
  base64 = {
    encode = base64_encode
  },
  parse_url = parse_url,
  generate_key = generate_key,
  read_int8 = read_int8,
  read_int16 = read_int16,
  read_int32 = read_int32,
  write_int8 = write_int8,
  write_int16 = write_int16,
  write_int32 = write_int32,
}
 end)
package.preload['websocket.frame'] = (function (...)
-- Following Websocket RFC: http://tools.ietf.org/html/rfc6455
local bit = require'websocket.bit'
local band = bit.band
local bxor = bit.bxor
local bor = bit.bor
local tremove = table.remove
local srep = string.rep
local ssub = string.sub
local sbyte = string.byte
local schar = string.char
local band = bit.band
local rshift = bit.rshift
local tinsert = table.insert
local tconcat = table.concat
local mmin = math.min
local mfloor = math.floor
local mrandom = math.random
local unpack = unpack or table.unpack
local tools = require'websocket.tools'
local write_int8 = tools.write_int8
local write_int16 = tools.write_int16
local write_int32 = tools.write_int32
local read_int8 = tools.read_int8
local read_int16 = tools.read_int16
local read_int32 = tools.read_int32

local bits = function(...)
  local n = 0
  for _,bitn in pairs{...} do
    n = n + 2^bitn
  end
  return n
end

local bit_7 = bits(7)
local bit_0_3 = bits(0,1,2,3)
local bit_0_6 = bits(0,1,2,3,4,5,6)

-- TODO: improve performance
local xor_mask = function(encoded,mask,payload)
  local transformed,transformed_arr = {},{}
  -- xor chunk-wise to prevent stack overflow.
  -- sbyte and schar multiple in/out values
  -- which require stack
  for p=1,payload,2000 do
    local last = mmin(p+1999,payload)
    local original = {sbyte(encoded,p,last)}
    for i=1,#original do
      local j = (i-1) % 4 + 1
      transformed[i] = bxor(original[i],mask[j])
    end
    local xored = schar(unpack(transformed,1,#original))
    tinsert(transformed_arr,xored)
  end
  return tconcat(transformed_arr)
end

local encode_header_small = function(header, payload)
  return schar(header, payload)
end

local encode_header_medium = function(header, payload, len)
  return schar(header, payload, band(rshift(len, 8), 0xFF), band(len, 0xFF))
end

local encode_header_big = function(header, payload, high, low)
  return schar(header, payload)..write_int32(high)..write_int32(low)
end

local encode = function(data,opcode,masked,fin)
  local header = opcode or 1-- TEXT is default opcode
  if fin == nil or fin == true then
    header = bor(header,bit_7)
  end
  local payload = 0
  if masked then
    payload = bor(payload,bit_7)
  end
  local len = #data
  local chunks = {}
  if len < 126 then
    payload = bor(payload,len)
    tinsert(chunks,encode_header_small(header,payload))
  elseif len <= 0xffff then
    payload = bor(payload,126)
    tinsert(chunks,encode_header_medium(header,payload,len))
  elseif len < 2^53 then
    local high = mfloor(len/2^32)
    local low = len - high*2^32
    payload = bor(payload,127)
    tinsert(chunks,encode_header_big(header,payload,high,low))
  end
  if not masked then
    tinsert(chunks,data)
  else
    local m1 = mrandom(0,0xff)
    local m2 = mrandom(0,0xff)
    local m3 = mrandom(0,0xff)
    local m4 = mrandom(0,0xff)
    local mask = {m1,m2,m3,m4}
    tinsert(chunks,write_int8(m1,m2,m3,m4))
    tinsert(chunks,xor_mask(data,mask,#data))
  end
  return tconcat(chunks)
end

local decode = function(encoded)
  local encoded_bak = encoded
  if #encoded < 2 then
    return nil,2-#encoded
  end
  local pos,header,payload
  pos,header = read_int8(encoded,1)
  pos,payload = read_int8(encoded,pos)
  local high,low
  encoded = ssub(encoded,pos)
  local bytes = 2
  local fin = band(header,bit_7) > 0
  local opcode = band(header,bit_0_3)
  local mask = band(payload,bit_7) > 0
  payload = band(payload,bit_0_6)
  if payload > 125 then
    if payload == 126 then
      if #encoded < 2 then
        return nil,2-#encoded
      end
      pos,payload = read_int16(encoded,1)
    elseif payload == 127 then
      if #encoded < 8 then
        return nil,8-#encoded
      end
      pos,high = read_int32(encoded,1)
      pos,low = read_int32(encoded,pos)
      payload = high*2^32 + low
      if payload < 0xffff or payload > 2^53 then
        assert(false,'INVALID PAYLOAD '..payload)
      end
    else
      assert(false,'INVALID PAYLOAD '..payload)
    end
    encoded = ssub(encoded,pos)
    bytes = bytes + pos - 1
  end
  local decoded
  if mask then
    local bytes_short = payload + 4 - #encoded
    if bytes_short > 0 then
      return nil,bytes_short
    end
    local m1,m2,m3,m4
    pos,m1 = read_int8(encoded,1)
    pos,m2 = read_int8(encoded,pos)
    pos,m3 = read_int8(encoded,pos)
    pos,m4 = read_int8(encoded,pos)
    encoded = ssub(encoded,pos)
    local mask = {
      m1,m2,m3,m4
    }
    decoded = xor_mask(encoded,mask,payload)
    bytes = bytes + 4 + payload
  else
    local bytes_short = payload - #encoded
    if bytes_short > 0 then
      return nil,bytes_short
    end
    if #encoded > payload then
      decoded = ssub(encoded,1,payload)
    else
      decoded = encoded
    end
    bytes = bytes + payload
  end
  return decoded,fin,opcode,encoded_bak:sub(bytes+1),mask
end

local encode_close = function(code,reason)
  if code then
    local data = write_int16(code)
    if reason then
      data = data..tostring(reason)
    end
    return data
  end
  return ''
end

local decode_close = function(data)
  local _,code,reason
  if data then
    if #data > 1 then
      _,code = read_int16(data,1)
    end
    if #data > 2 then
      reason = data:sub(3)
    end
  end
  return code,reason
end

return {
  encode = encode,
  decode = decode,
  encode_close = encode_close,
  decode_close = decode_close,
  encode_header_small = encode_header_small,
  encode_header_medium = encode_header_medium,
  encode_header_big = encode_header_big,
  CONTINUATION = 0,
  TEXT = 1,
  BINARY = 2,
  CLOSE = 8,
  PING = 9,
  PONG = 10
}
 end)
package.preload['websocket.bit'] = (function (...)
local has_bit32,bit = pcall(require,'bit32')
if has_bit32 then
  -- lua 5.2 / bit32 library
  bit.rol = bit.lrotate
  bit.ror = bit.rrotate
  return bit
else
  -- luajit / lua 5.1 + luabitop
  return require'bit'
end
 end)
package.preload['websocket.client_ev'] = (function (...)

local socket = require'socket'
local tools = require'websocket.tools'
local frame = require'websocket.frame'
local handshake = require'websocket.handshake'
local debug = require'debug'
local tconcat = table.concat
local tinsert = table.insert

local ev = function(ws)
  ws = ws or {}
  local ev = require'ev'
  local sock
  local loop = ws.loop or ev.Loop.default
  local fd
  local message_io
  local handshake_io
  local send_io_stop
  local async_send
  local self = {}
  self.state = 'CLOSED'
  local close_timer
  local user_on_message
  local user_on_close
  local user_on_open
  local user_on_error
  local cleanup = function()
    if close_timer then
      close_timer:stop(loop)
      close_timer = nil
    end
    if handshake_io then
      handshake_io:stop(loop)
      handshake_io:clear_pending(loop)
      handshake_io = nil
    end
    if send_io_stop then
      send_io_stop()
      send_io_stop = nil
    end
    if message_io then
      message_io:stop(loop)
      message_io:clear_pending(loop)
      message_io = nil
    end
    if sock then
      sock:shutdown()
      sock:close()
      sock = nil
    end
  end

  local on_close = function(was_clean,code,reason)
    cleanup()
    self.state = 'CLOSED'
    if user_on_close then
      user_on_close(self,was_clean,code,reason or '')
    end
  end
  local on_error = function(err,dont_cleanup)
    if not dont_cleanup then
      cleanup()
    end
    if user_on_error then
      user_on_error(self,err)
    else
      print('Error',err)
    end
  end
  local on_open = function(_,headers)
    self.state = 'OPEN'
    if user_on_open then
      user_on_open(self,headers['sec-websocket-protocol'],headers)
    end
  end
  local handle_socket_err = function(err,io,sock)
    if self.state == 'OPEN' then
      on_close(false,1006,err)
    elseif self.state ~= 'CLOSED' then
      on_error(err)
    end
  end
  local on_message = function(message,opcode)
    if opcode == frame.TEXT or opcode == frame.BINARY then
      if user_on_message then
        user_on_message(self,message,opcode)
      end
    elseif opcode == frame.CLOSE then
      if self.state ~= 'CLOSING' then
        self.state = 'CLOSING'
        local code,reason = frame.decode_close(message)
        local encoded = frame.encode_close(code)
        encoded = frame.encode(encoded,frame.CLOSE,true)
        async_send(encoded,
          function()
            on_close(true,code or 1005,reason)
          end,handle_socket_err)
      else
        on_close(true,1005,'')
      end
    end
  end

  self.send = function(_,message,opcode)
    local encoded = frame.encode(message,opcode or frame.TEXT,true)
    async_send(encoded, nil, handle_socket_err)
  end

  self.connect = function(_,url,ws_protocol)
    if self.state ~= 'CLOSED' then
      on_error('wrong state',true)
      return
    end
    local protocol,host,port,uri = tools.parse_url(url)
    if protocol ~= 'ws' then
      on_error('bad protocol')
      return
    end
    local ws_protocols_tbl = {''}
    if type(ws_protocol) == 'string' then
        ws_protocols_tbl = {ws_protocol}
    elseif type(ws_protocol) == 'table' then
        ws_protocols_tbl = ws_protocol
    end
    self.state = 'CONNECTING'
    assert(not sock)
    sock = socket.tcp()
    fd = sock:getfd()
    assert(fd > -1)
    -- set non blocking
    sock:settimeout(0)
    sock:setoption('tcp-nodelay',true)
    async_send,send_io_stop = require'websocket.ev_common'.async_send(sock,loop)
    handshake_io = ev.IO.new(
      function(loop,connect_io)
        connect_io:stop(loop)
        local key = tools.generate_key()
        local req = handshake.upgrade_request
        {
          key = key,
          host = host,
          port = port,
          protocols = ws_protocols_tbl,
          origin = ws.origin,
          uri = uri
        }
        async_send(
          req,
          function()
            local resp = {}
            local response = ''
            local read_upgrade = function(loop,read_io)
              -- this seems to be possible, i don't understand why though :(
              if not sock then
                read_io:stop(loop)
                handshake_io = nil
                return
              end
              repeat
                local byte,err,pp = sock:receive(1)
                if byte then
                  response = response..byte
                elseif err then
                  if err == 'timeout' then
                    return
                  else
                    read_io:stop(loop)
                    on_error('accept failed')
                    return
                  end
                end
              until response:sub(#response-3) == '\r\n\r\n'
              read_io:stop(loop)
              handshake_io = nil
              local headers = handshake.http_headers(response)
              local expected_accept = handshake.sec_websocket_accept(key)
              if headers['sec-websocket-accept'] ~= expected_accept then
                self.state = 'CLOSED'
                on_error('accept failed')
                return
              end
              message_io = require'websocket.ev_common'.message_io(
                sock,loop,
                on_message,
              handle_socket_err)
              on_open(self, headers)
            end
            handshake_io = ev.IO.new(read_upgrade,fd,ev.READ)
            handshake_io:start(loop)-- handshake
          end,
        handle_socket_err)
      end,fd,ev.WRITE)
    local connected,err = sock:connect(host,port)
    if connected then
      handshake_io:callback()(loop,handshake_io)
    elseif err == 'timeout' or err == 'Operation already in progress' then
      handshake_io:start(loop)-- connect
    else
      self.state = 'CLOSED'
      on_error(err)
    end
  end

  self.on_close = function(_,on_close_arg)
    user_on_close = on_close_arg
  end

  self.on_error = function(_,on_error_arg)
    user_on_error = on_error_arg
  end

  self.on_open = function(_,on_open_arg)
    user_on_open = on_open_arg
  end

  self.on_message = function(_,on_message_arg)
    user_on_message = on_message_arg
  end

  self.close = function(_,code,reason,timeout)
    if handshake_io then
      handshake_io:stop(loop)
      handshake_io:clear_pending(loop)
    end
    if self.state == 'CONNECTING' then
      self.state = 'CLOSING'
      on_close(false,1006,'')
      return
    elseif self.state == 'OPEN' then
      self.state = 'CLOSING'
      timeout = timeout or 3
      local encoded = frame.encode_close(code or 1000,reason)
      encoded = frame.encode(encoded,frame.CLOSE,true)
      -- this should let the other peer confirm the CLOSE message
      -- by 'echoing' the message.
      async_send(encoded)
      close_timer = ev.Timer.new(function()
          close_timer = nil
          on_close(false,1006,'timeout')
        end,timeout)
      close_timer:start(loop)
    end
  end

  return self
end

return ev
 end)
package.preload['websocket.ev_common'] = (function (...)
local ev = require'ev'
local frame = require'websocket.frame'
local tinsert = table.insert
local tconcat = table.concat
local eps = 2^-40

local detach = function(f,loop)
  if ev.Idle then
    ev.Idle.new(function(loop,io)
        io:stop(loop)
        f()
      end):start(loop)
  else
    ev.Timer.new(function(loop,io)
        io:stop(loop)
        f()
      end,eps):start(loop)
  end
end

local async_send = function(sock,loop)
  assert(sock)
  loop = loop or ev.Loop.default
  local sock_send = sock.send
  local buffer
  local index
  local callbacks = {}
  local send = function(loop,write_io)
    local len = #buffer
    local sent,err,last = sock_send(sock,buffer,index)
    if not sent and err ~= 'timeout' then
      write_io:stop(loop)
      if callbacks.on_err then
        if write_io:is_active() then
          callbacks.on_err(err)
        else
          detach(function()
              callbacks.on_err(err)
            end,loop)
        end
      end
    elseif sent then
      local copy = buffer
      buffer = nil
      index = nil
      write_io:stop(loop)
      if callbacks.on_sent then
        -- detach calling callbacks.on_sent from current
        -- exection if thiis call context is not
        -- the send io to let send_async(_,on_sent,_) truely
        -- behave async.
        if write_io:is_active() then
          
          callbacks.on_sent(copy)
        else
          -- on_sent is only defined when responding to "on message for close op"
          -- so this can happen only once per lifetime of a websocket instance.
          -- callbacks.on_sent may be overwritten by a new call to send_async
          -- (e.g. due to calling ws:close(...) or ws:send(...))
          local on_sent = callbacks.on_sent
          detach(function()
              on_sent(copy)
            end,loop)
        end
      end
    else
      assert(last < len)
      index = last + 1
    end
  end
  local io = ev.IO.new(send,sock:getfd(),ev.WRITE)
  local stop = function()
    io:stop(loop)
    buffer = nil
    index = nil
  end
  local send_async = function(data,on_sent,on_err)
    if buffer then
      -- a write io is still running
      buffer = buffer..data
      return #buffer
    else
      buffer = data
    end
    callbacks.on_sent = on_sent
    callbacks.on_err = on_err
    if not io:is_active() then
      send(loop,io)
      if index ~= nil then
        io:start(loop)
      end
    end
    local buffered = (buffer and #buffer - (index or 0)) or 0
    return buffered
  end
  return send_async,stop
end

local message_io = function(sock,loop,on_message,on_error)
  assert(sock)
  assert(loop)
  assert(on_message)
  assert(on_error)
  local last
  local frames = {}
  local first_opcode
  assert(sock:getfd() > -1)
  local message_io
  local dispatch = function(loop,io)
    -- could be stopped meanwhile by on_message function
    while message_io:is_active() do
      local encoded,err,part = sock:receive(100000)
      if err then
        if err == 'timeout' and #part == 0 then
          return
        elseif #part == 0 then
          if message_io then
            message_io:stop(loop)
          end
          on_error(err,io,sock)
          return
        end
      end
      if last then
        encoded = last..(encoded or part)
        last = nil
      else
        encoded = encoded or part
      end
      
      repeat
        local decoded,fin,opcode,rest = frame.decode(encoded)
        if decoded then
          if not first_opcode then
            first_opcode = opcode
          end
          tinsert(frames,decoded)
          encoded = rest
          if fin == true then
            on_message(tconcat(frames),first_opcode)
            frames = {}
            first_opcode = nil
          end
        end
      until not decoded
      if #encoded > 0 then
        last = encoded
      end
    end
  end
  message_io = ev.IO.new(dispatch,sock:getfd(),ev.READ)
  message_io:start(loop)
  -- the might be already data waiting (which will not trigger the IO)
  dispatch(loop,message_io)
  return message_io
end

return {
  async_send = async_send,
  message_io = message_io
}
 end)
package.preload['websocket.server_ev'] = (function (...)

local socket = require'socket'
local tools = require'websocket.tools'
local frame = require'websocket.frame'
local handshake = require'websocket.handshake'
local tconcat = table.concat
local tinsert = table.insert
local ev
local loop

local clients = {}
clients[true] = {}

local client = function(sock,protocol)
  assert(sock)
  sock:setoption('tcp-nodelay',true)
  local fd = sock:getfd()
  local message_io
  local close_timer
  local async_send = require'websocket.ev_common'.async_send(sock,loop)
  local self = {}
  self.state = 'OPEN'
  self.sock = sock
  local user_on_error
  local on_error = function(s,err)
    if clients[protocol] ~= nil and clients[protocol][self] ~= nil then
      clients[protocol][self] = nil
    end
    if user_on_error then
      user_on_error(self,err)
    else
      print('Websocket server error',err)
    end
  end
  local user_on_close
  local on_close = function(was_clean,code,reason)
    if clients[protocol] ~= nil and clients[protocol][self] ~= nil then
      clients[protocol][self] = nil
    end
    if close_timer then
      close_timer:stop(loop)
      close_timer = nil
    end
    message_io:stop(loop)
    self.state = 'CLOSED'
    if user_on_close then
      user_on_close(self,was_clean,code,reason or '')
    end
    sock:shutdown()
    sock:close()
  end
  
  local handle_sock_err = function(err)
    if err == 'closed' then
      if self.state ~= 'CLOSED' then
        on_close(false,1006,'')
      end
    else
      on_error(err)
    end
  end
  local user_on_message = function() end
  local TEXT = frame.TEXT
  local BINARY = frame.BINARY
  local on_message = function(message,opcode)
    if opcode == TEXT or opcode == BINARY then
      user_on_message(self,message,opcode)
    elseif opcode == frame.CLOSE then
      if self.state ~= 'CLOSING' then
        self.state = 'CLOSING'
        local code,reason = frame.decode_close(message)
        local encoded = frame.encode_close(code)
        encoded = frame.encode(encoded,frame.CLOSE)
        async_send(encoded,
          function()
            on_close(true,code or 1006,reason)
          end,handle_sock_err)
      else
        on_close(true,1006,'')
      end
    end
  end
  
  self.send = function(_,message,opcode)
    local encoded = frame.encode(message,opcode or frame.TEXT)
    return async_send(encoded)
  end
  
  self.on_close = function(_,on_close_arg)
    user_on_close = on_close_arg
  end
  
  self.on_error = function(_,on_error_arg)
    user_on_error = on_error_arg
  end
  
  self.on_message = function(_,on_message_arg)
    user_on_message = on_message_arg
  end
  
  self.broadcast = function(_,...)
    for client in pairs(clients[protocol]) do
      if client.state == 'OPEN' then
        client:send(...)
      end
    end
  end
  
  self.close = function(_,code,reason,timeout)
    if clients[protocol] ~= nil and clients[protocol][self] ~= nil then
      clients[protocol][self] = nil
    end
    if not message_io then
      self:start()
    end
    if self.state == 'OPEN' then
      self.state = 'CLOSING'
      assert(message_io)
      timeout = timeout or 3
      local encoded = frame.encode_close(code or 1000,reason or '')
      encoded = frame.encode(encoded,frame.CLOSE)
      async_send(encoded)
      close_timer = ev.Timer.new(function()
          close_timer = nil
          on_close(false,1006,'timeout')
        end,timeout)
      close_timer:start(loop)
    end
  end
  
  self.start = function()
    message_io = require'websocket.ev_common'.message_io(
      sock,loop,
      on_message,
    handle_sock_err)
  end
  
  
  return self
end

local listen = function(opts)
  assert(opts and (opts.protocols or opts.default))
  ev = require'ev'
  loop = opts.loop or ev.Loop.default
  local user_on_error
  local on_error = function(s,err)
    if user_on_error then
      user_on_error(s,err)
    else
      print(err)
    end
  end
  local protocols = {}
  if opts.protocols then
    for protocol in pairs(opts.protocols) do
      clients[protocol] = {}
      tinsert(protocols,protocol)
    end
  end
  local self = {}
  self.on_error = function(self,on_error)
    user_on_error = on_error
  end
  local listener,err = socket.bind(opts.interface or '*',opts.port or 80)
  if not listener then
    error(err)
  end
  listener:settimeout(0)
  
  self.sock = function()
    return listener
  end
  
  local listen_io = ev.IO.new(
    function()
      local client_sock = listener:accept()
      client_sock:settimeout(0)
      assert(client_sock)
      local request = {}
      local last
      ev.IO.new(
        function(loop,read_io)
          repeat
            local line,err,part = client_sock:receive('*l')
            if line then
              if last then
                line = last..line
                last = nil
              end
              request[#request+1] = line
            elseif err ~= 'timeout' then
              on_error(self,'Websocket Handshake failed due to socket err:'..err)
              read_io:stop(loop)
              return
            else
              last = part
              return
            end
          until line == ''
          read_io:stop(loop)
          local upgrade_request = tconcat(request,'\r\n')
          local response,protocol = handshake.accept_upgrade(upgrade_request,protocols)
          if not response then
            print('Handshake failed, Request:')
            print(upgrade_request)
            client_sock:close()
            return
          end
          local index
          ev.IO.new(
            function(loop,write_io)
              local len = #response
              local sent,err = client_sock:send(response,index)
              if not sent then
                write_io:stop(loop)
                print('Websocket client closed while handshake',err)
              elseif sent == len then
                write_io:stop(loop)
                local protocol_handler
                local new_client
                local protocol_index
                if protocol and opts.protocols[protocol] then
                  protocol_index = protocol
                  protocol_handler = opts.protocols[protocol]
                elseif opts.default then
                  -- true is the 'magic' index for the default handler
                  protocol_index = true
                  protocol_handler = opts.default
                else
                  client_sock:close()
                  if on_error then
                    on_error('bad protocol')
                  end
                  return
                end
                new_client = client(client_sock,protocol_index)
                clients[protocol_index][new_client] = true
                protocol_handler(new_client)
                new_client:start(loop)
              else
                assert(sent < len)
                index = sent
              end
            end,client_sock:getfd(),ev.WRITE):start(loop)
        end,client_sock:getfd(),ev.READ):start(loop)
    end,listener:getfd(),ev.READ)
  self.close = function(keep_clients)
    listen_io:stop(loop)
    listener:close()
    listener = nil
    if not keep_clients then
      for protocol,clients in pairs(clients) do
        for client in pairs(clients) do
          client:close()
        end
      end
    end
  end
  listen_io:start(loop)
  return self
end

return {
  listen = listen
}
 end)
local frame = require'websocket.frame'

return {
  client = require'websocket.client',
  server = require'websocket.server',
  CONTINUATION = frame.CONTINUATION,
  TEXT = frame.TEXT,
  BINARY = frame.BINARY,
  CLOSE = frame.CLOSE,
  PING = frame.PING,
  PONG = frame.PONG
}
