require 'coxpcall'
local socket = require'socket'
local copas = require'copas'
local tools = require'websocket.tools'
local frame = require'websocket.frame'
local handshake = require'websocket.handshake'
local sync = require'websocket.sync'
local tconcat = table.concat
local tinsert = table.insert

local json = require('json')

local clients = {}

local client = function(sock,protocol)
  local copas = require'copas'
  
  local self = {}
  
  self.state = 'OPEN'
  self.is_server = true
  self.queued_messages = {}
  
  self.sock_send = function(self,...)
    print("[XX] calling copas.send!")
    return copas.send(sock,...)
  end
  
  self.sock_receive = function(self,...)
    print("[XX] calling copas.receive!")
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

  self.dsend = function(self,data,opcode)
    if self.state ~= 'OPEN' then
      return nil,false,1006,'wrong state'
    end
    local encoded = frame.encode(data,opcode or frame.TEXT,not self.is_server)
    local n,err = sock:send(encoded)
    if n ~= #encoded then
      return nil,self:close(1006,err)
    end
    return true
  end

  self.queue_message = function(self, message)
    table.insert(self.queued_messages, message)
  end

  self.has_queued_messages = function(self)
    return table.getn(self.queued_messages) > 0
  end

  self.send_queued_messages = function(self)
    while table.getn(self.queued_messages) > 0 do
      local msg = json.encode(table.remove(self.queued_messages, 1))
      print("[XX] SENDING MESSAGE: " .. msg)
      self:send(msg)
    end
    print("[XX] QUEUE SENT")
  end

  return self
end

local ws_server_create = function (opts)
  assert(opts and (opts.protocols or opts.default))
  local on_error = opts.on_error or function(s) print(s) end
  if err then
    print("[XX] error err")
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

  local self = {}
  self.add_client = function(request, raw_sock, copas_sock, main_handler)
      --local request = {}
      --repeat
      --  -- no timeout used, so should either return with line or err
      --  local line,err = sock:receive('*l')
      --  if line then
      --    request[#request+1] = line
      --  else
      --    sock:close()
      --    if on_error then
      --      on_error('invalid request')
      --    end
      --    return
      --  end
      --until line == ''
      local upgrade_request = tconcat(request,'\r\n')
      print("[XX] REQUEST:")
      print(upgrade_request)
      print("[XX] END OF REQUEST")
      local response,protocol = handshake.accept_upgrade(upgrade_request,protocols)
      local status = true
      --local status,response,protocol = pcall(handshake.accept_upgrade, upgrade_request,protocols)
      if not status then
        print("[XX] error: Client request does not appear to be a websocket\n")
        --copas.send(sock,protocol)
        --sock:close()
        return nil, "Client request does not appear to be a websocket"
      end
      print("[XX] ABOUT TO SEND OK RESPONSE (status was " .. json.encode(status) .. ")")
      if sock ~= nil then
        print("[XX] HAVE SOCK")
      else
        print("[XX] SOCK NIL")
      end
      if resp == nil then
        print("[XX] COULD NOT UPGRADE REQUEST")
        print(protocol)
      end
      print("[XX] RESP: " .. json.encode(response))
      copas.send(copas_sock, response)
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
        copas_sock:close()
        if on_error then
          on_error('bad protocol, and no default set')
        end
        return nil, 'Unknown protocol and no default protocol set'
      end
      new_client = client(copas_sock,protocol_index)
      clients[protocol_index][new_client] = true
      --handler(new_client)
      print("[XX] ADD TO SERVER1")
      main_handler:do_add_ws_c(new_client)
      print("[XX] ADDED TO SERVER!")
      -- this is a dirty trick for preventing
      -- copas from automatically and prematurely closing
      -- the socket
      --print("[XX] STARTING ETERNAL LOOP. SEE YOU AT THE END OF THE UNIVERSE")
      --while new_client.state ~= 'CLOSED' do
      --  local dummy = {
      --    send = function() end,
      --    close = function() end
      --  }
      --  copas.send(dummy)
      --end
      --print("[XX] LOOP ENDED ANYWAY")
      return new_client
    end

  self.close = function(_,keep_clients)
    --copas.removeserver(listener)
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

local _M = {}
_M.ws_server_create = ws_server_create
return _M

--return {
--  ws_server_create = ws_server_create
--}
