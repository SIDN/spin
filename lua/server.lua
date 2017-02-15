#!/usr/bin/env lua
--- lua websocket equivalent to test-server.c from libwebsockets.
-- using copas as server framework

package.path = '../src/?.lua;../src/?/?.lua;'..package.path
local copas = require'copas'
local socket = require'socket'
local P = require'posix'

local inc_clients = {}

local websocket = require'websocket'

local server = websocket.server.copas.listen
{
  protocols = {
    ['lws-mirror-protocol'] = function(ws)
      while true do
        local msg,opcode = ws:receive()
        if not msg then
          ws:close()
          return
        end
        if opcode == websocket.TEXT then
          ws:broadcast(msg)
        end
      end
    end,
    ['dumb-increment-protocol'] = function(ws)
      inc_clients[ws] = 0
      while true do
        local message,opcode = ws:receive()
        if not message then
          ws:close()
          inc_clients[ws] = nil
          return
        end
        if opcode == websocket.TEXT then
          if message:match('reset') then
            inc_clients[ws] = 0
          end
        end
      end
    end
  },
  port = 12345
}

local pid_file_name = "/tmp/TESTPIDFILE.pid"
function remove_pidfile()
    print("yo")
    --os.remove(pid_file_name)
    os.exit()
end

-- TODO: move what we can to the module

function test_print(msg)
  print(msg)
end

function create_callback(ic)
  return function(msg)
    for ws,number in pairs(ic) do
      --ws:send(msg)
      ws:send(msg)
      ic[ws] = number + 1
    end
  end
end

function send_number_loop()
    local last = socket.gettime()                    
    while true do                             
      copas.step(0.1)
      local now = socket.gettime()            
      if (now - last) >= 0.1 then             
        last = now                            
        for ws,number in pairs(inc_clients) do
          ws:send(tostring(number))       
          inc_clients[ws] = number + 1    
        end                               
      end                                 
    end       
end

function collector_loop()
    local fd1 = P.open("/tmp/spin_pipe", bit.bor(P.O_RDONLY, P.O_NONBLOCK))
    --local fd2 = P.open(arg[2], P.O_RDONLY)
                                            
    cb = create_callback(inc_clients)     
    --cb = test_print
    while true do                           
        --print("collector loop")           
        copas.step(0.1)                         
        handle_pipe_output(fd1, cb)
    end
end

local collector = require'collect'

print("add thread")
copas.addthread(
collector_loop
)
print("add thread done")


copas.loop()
