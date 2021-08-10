--
-- This is a SPIN-specific DNS cache
--
-- It stores information about A/AAAA query answers,
-- keyed by the answer *value*
--
-- The purpose is to (shortly) remember what DNS queries IP traffic
-- was triggered by.
--

local verbose = false

local _M = {}

function _M.vprint(msg)
    if verbose then
        print("[SPIN/DNSCache] " .. msg)
    end
end

local DNSCacheEntry = {}
DNSCacheEntry.__index = DNSCacheEntry

function _M.DNSCacheEntry_create()
  local cache_entry = {}
  setmetatable(cache_entry, DNSCacheEntry)
  cache_entry.domains = {}
  cache_entry.size = 0
  return cache_entry
end

function DNSCacheEntry:add_domain(domain, timestamp)
  local existing_domain = self.domains[domain]
  if existing_domain == nil then
    self.size = self.size + 1
  end
  self.domains[domain] = timestamp
end

function DNSCacheEntry:clean(clean_before)
  for domain,timestamp in pairs(self.domains) do
    if timestamp < clean_before then
      self.domains[domain] = nil
      self.size = self.size - 1
    end
  end
end

function DNSCacheEntry:print(out)
  for domain, timestamp in pairs(self.domains) do
    out:write("    " .. timestamp .. " " .. domain .. "\n")
  end
end

local DNSCache = {}
DNSCache.__index = DNSCache

function _M.DNSCache_create()
    local cache = {}
    setmetatable(cache, DNSCache)
    cache.entries = {}
    cache.size = 0
    return cache
end

function DNSCache:add(address, domain, timestamp)
  _M.vprint("Add to cache: " .. address .. " " .. domain .. " at " .. timestamp)
  local entry = self.entries[address]
  if entry == nil then
    entry = _M.DNSCacheEntry_create()
    self.size = self.size + 1
    --return true
  end
  entry:add_domain(domain, timestamp)
  self.entries[address] = entry
  -- return false if now new?
  return true
end

function DNSCache:clean(clean_before)
  for address, entry in pairs(self.entries) do
    entry:clean(clean_before)
    if entry.size == 0 then
      self.entries[address] = nil
      self.size = self.size - 1
    end
  end
  _M.vprint("Cache size: " .. self.size)
end

function DNSCache:get_domains(address)
  local result = {}
  local entry = self.entries[address]
  if entry ~= nil then
    for domain,_ in pairs(entry.domains) do
      table.insert(result, domain)
    end
  else
    _M.vprint("no cache entry for " .. address)
  end
  return result
end

function DNSCache:print(out)
  out:write("------- FULL DNS CACHE -------\n")
  out:write("  containing " .. self.size .. " entries.\n")
  for address, entry in pairs(self.entries) do
    out:write(address .. "\n")
    entry:print(out)
  end
  out:write("------ END OF DNS CACHE ------\n")
end

-- There is one global cache for ease of use
-- (but users are free to initialize their own)
_M.dnscache = _M.DNSCache_create()

return _M
