--
-- Wrapper for posix commands, with some more
-- advanced and some more specific functions than
-- luaposix (such as full pipe control)
--


local posix = require 'posix'
local bit = require 'bit'

local mio = {}

--
-- This modules contains wrappers around the posix library
-- for easier reading of files and running of subprocesses
--
-- The io library has some issues on openwrt with lua 5.1
--

--
-- baseline utility functions
--

-- split a command into an executable and a table of arguments
-- assumes no spaces are used in the arguments (no quoting supported)
-- for safety, also do not compound arguments (write -ade as -a -d -e)
function mio.split_cmd(command)
  local cmd = nil
  local args = {}
  for el in string.gmatch(command, "%S+") do
    if cmd == nil then cmd = el else table.insert(args, el) end
  end
  return cmd, args
end

-- read a line from the given file descriptor
function mio.read_fd_line(fd, strip_newline, timeout)
  if fd == nil then
    return nil, "Read on closed file"
  end
  if timeout == nil then timeout = 500 end
  local pr, err = posix.rpoll(fd, timeout)
  if pr == nil then return nil, err end
  if pr == 0 then
    return nil, "read timed out"
  end
  local result = ""
  while true do
    local c = posix.read(fd, 1)
    if c == posix.EOF or c == '' then
      if result == "" then
        return nil
      else
        if strip_newline then return result else return result .. "\n" end
      end
    elseif c == "\n" then
      if strip_newline then return result else return result .. "\n" end
    else
      result = result .. c
    end
  end
end

function mio.write_fd_line(fd, line, add_newline)
  if fd == nil then
    return nil, "Write on closed file"
  end
  if add_newline then line = line .. "\n" end
  return posix.write(fd, line)
end

--
-- Simple popen3() implementation
--
function mio.popen3(path, args, delay, pipe_stdout, pipe_stderr, pipe_stdin)
    if args == nil then args = {} end
    -- w1 = process stdin (we write to it)
    -- r2 = process stdout (we read from it)
    -- r3 = process stderr (we read from it)
    -- can be nil depending on arguments
    local r1,w1,r2,w2,r3,w3
    if pipe_stdin then r1, w1 = posix.pipe() end
    if pipe_stdout then r2, w2 = posix.pipe() end
    if pipe_stderr then r3, w3 = posix.pipe() end

    --assert((w1 ~= nil or r2 ~= nil or r3 ~= nil), "pipe() failed")

    local pid, fork_err = posix.fork()
    assert(pid ~= nil, "fork() failed")
    if pid == 0 then
        if delay then posix.sleep(delay) end
        if pipe_stdin then
          posix.close(w1)
          posix.dup2(r1, posix.fileno(io.stdin))
          posix.close(r1)
        end

        if pipe_stdout then
          posix.close(r2)
          posix.dup2(w2, posix.fileno(io.stdout))
          posix.close(w2)
        end

        if pipe_stderr then
          posix.dup2(w3, posix.fileno(io.stderr))
          posix.close(w3)
        end

        local ret, exec_err = posix.execp(path, args)
        assert(ret ~= nil, "execp() failed for '" .. path .. "': " .. exec_err)

        posix._exit(1)
        return
    end

    if pipe_stdin then posix.close(r1) end
    if pipe_stdout then posix.close(w2) end
    if pipe_stderr then posix.close(w3) end

    return pid, w1, r2, r3
end

local function strjoin(delimiter, list)
   local len = 0
   if list then len = table.getn(list) end
   if len == 0 then
      return ""
   elseif len == 1 then
      return list[1]
   else
     local string = list[1]
     for i = 2, len do
        string = string .. delimiter .. list[i]
     end
     return string
   end
end


local subprocess = {}
subprocess.__index = subprocess

function mio.subprocess(path, args, delay, pipe_stdout, pipe_stderr, pipe_stdin)
  local subp = {}             -- our new object
  setmetatable(subp,subprocess)  -- make Account handle lookup
  subp.path = path
  subp.args = args
  subp.delay = delay
  subp.pipe_stdout = pipe_stdout
  subp.pipe_stderr = pipe_stderr
  subp.pipe_stdin = pipe_stdin
  return subp:start()
end

function subprocess:start()
  self.pid, self.stdin, self.stdout, self.stderr = mio.popen3(self.path, self.args, self.delay, self.pipe_stdout, self.pipe_stderr, self.pipe_stdin)
  -- todo: error?
  return self
end

function subprocess:read_line(strip_newline, timeout)
  if self.pid == nil then
    return nil, "read_line() from stopped child process"
  end
  local result = mio.read_fd_line(self.stdout, strip_newline, timeout)
  return result
end

function subprocess:read_lines(strip_newlines, timeout)
  local result = {}
  for line in self:read_line_iterator(strip_newlines, timeout) do
    table.insert(result, line)
  end
  return result
end

function subprocess:read_line_iterator(strip_newlines, timeout)
  local function next_line()
    return self:read_line(strip_newlines, timeout)
  end
  return next_line
end

function subprocess:readline_stderr(strip_newline)
  return mio.read_fd_line(self.stderr, strip_newline)
end

function subprocess:write_line(line, add_newline)
  return mio.write_fd_line(self.stdin, line, add_newline)
end

function subprocess:wait()
  -- does this leave fd's open?
  local spid, state, rcode = posix.wait(self.pid)
  if spid == self.pid then
    self.rcode = rcode
    self.pid = nil
    self.stdin = nil
    self.stdout = nil
    self.stderr = nil
  end
  return rcode
end

function subprocess:close()
  if self.stdin then posix.close(self.stdin) end
  if self.stdout then posix.close(self.stdout) end
  if self.stderr then posix.close(self.stderr) end
  if self.pid then return self:wait() else return self.rcode end
end


-- text file reading
local filereader = {}
filereader.__index = filereader

function mio.file_reader(filename)
  local fr = {}             -- our new object
  setmetatable(fr,filereader)  -- make Account handle lookup
  fr.filename = filename
  local r, err = fr:open()
  if r == nil then return nil, err end
  return fr
end

function filereader:open()
  local err
  self.fd, err = posix.open(self.filename, posix.O_RDONLY)
  if self.fd == nil then return nil, err end
  return self
end

-- read one line from the file
function filereader:read_line(strip_newline)
  local result = mio.read_fd_line(self.fd, strip_newline)
  if result == nil then self:close() end
  return result
end

-- return the lines of the file as an iterator
function filereader:read_line_iterator(strip_newlines)
  local function next_line()
    return self:read_line(strip_newlines)
  end
  return next_line
end

-- read all lines and return as a list
function filereader:read_lines(strip_newlines, timeout)
  local result = {}
  for line in self:read_line_iterator(strip_newlines, timeout) do
    table.insert(result, line)
  end
  return result
end


function filereader:close()
  if self.fd then
    posix.close(self.fd)
    self.fd = nil
  end
end

-- if drop_output is true; the stdout and the stderr
-- of the process will be ignored; if not, they will
-- be passed on to our own stdout and stderr
function mio.execute(command, drop_output)
  local cmd, args = mio.split_cmd(command)
  local subp = mio.subprocess(cmd, args, nil, drop_output, drop_output, false)
  return subp:close()
end

-- text file reading
local filewriter = {}
filewriter.__index = filewriter

function mio.file_writer(filename)
  local fw = {}             -- our new object
  setmetatable(fw,filewriter)  -- make Account handle lookup
  fw.filename = filename
  local r, err = fw:open()
  if r == nil then return nil, err end
  return fw
end

function filewriter:open()
  local err
  self.fd, err = posix.open(self.filename, bit.bor(posix.O_CREAT, posix.O_WRONLY), 600)
  if self.fd == nil then return nil, err end
  return self
end

function filewriter:write_line(line, add_newline)
  if self.fd == nil then
    return nil, "Write on closed file"
  end
  return mio.write_fd_line(self.fd, line, add_newline)
end

-- write all the lines from the given iterator
function filewriter:write_line_iterator(iterator)
  if self.fd == nil then
    return nil, "Write on closed file"
  end
  for line in iterator() do
    posix.write(self.fd, line)
  end
  return true
end

-- write all lines from the given list
function filewriter:write_lines(list)
  if self.fd == nil then
    return nil, "Write on closed file"
  end
  for _,line in ipairs(list) do
    posix.write(self.fd, line)
  end
  return true
end

function filewriter:close()
  if self.fd then
    posix.close(self.fd)
    self.fd = nil
  end
end

function mio.file_last_modified(filename)
  local result, err = posix.stat(filename)
  if result == nil then return nil, err end
  return result.mtime
end

return mio
