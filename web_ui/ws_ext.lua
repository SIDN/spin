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
  self.add_client = function(request, sock, main_handler)
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
      --local status,response,protocol = handshake.accept_upgrade(upgrade_request,protocols)
      local status,response,protocol = pcall(handshake.accept_upgrade, upgrade_request,protocols)
      if not status then
        print("[XX] error: Client request does not appear to be a websocket\n")
        --copas.send(sock,protocol)
        --sock:close()
        return nil, "Client request does not appear to be a websocket"
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
          on_error('bad protocol, and no default set')
        end
        return nil, 'Unknown protocol and no default protocol set'
      end
      new_client = client(sock,protocol_index)
      clients[protocol_index][new_client] = true
      --handler(new_client)
      print("[XX] ADD TO SERVER")
      main_handler:do_add_ws_c(new_client)
      print("[XX] ADDED TO SERVER")
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
      return true
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
