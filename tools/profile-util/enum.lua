-- Source: https://github.com/Tjakka5/Enum
--
-- MIT License
-- 
-- Copyright (c) 2017 Justin van der Leij
-- 
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
-- 
-- The above copyright notice and this permission notice shall be included in all
-- copies or substantial portions of the Software.
-- 
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
-- SOFTWARE.

local Enum = {}
local Meta = {
   __index    = function(_, k) error("Attempt to index non-existant enum '"..tostring(k).."'.", 2) end,
   __newindex = function()     error("Attempt to write to static enum", 2) end,
}

function Enum.new(...)
   local values = {...}

   if type(values[1]) == "table" then
      values = values[1]
   end

   local enum = {}

   for i = 1, #values do
      enum[values[i]] = values[i]
   end

   return setmetatable(enum, Meta)
end

return setmetatable(Enum, {
   __call  = function(_, ...) return Enum.new(...) end,
})
