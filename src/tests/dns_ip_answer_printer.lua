#!/usr/bin/lua

local lnflog = require 'lua-spin_nflog'
local bit = require 'bit'


--print(lnflog.sin(lnflog.pi))

function print_array(arr)
    io.write("0:   ")
    for i,x in pairs(arr) do
      io.write(x)
      if (i == 0 or i % 10 == 0) then
        print("")
        io.write(i .. ": ")
        if (i < 100) then
          io.write(" ")
        end
      else
        io.write(" ")
      end
    end
    print("")
end

function get_dns_qname(event)
    local result = ""
    local cur_di = 40
    local labellen = event:get_octet(cur_di)

    cur_di = cur_di + 1
    while labellen > 0 do
      label_ar, err = event:get_octets(cur_di, labellen)
      if label_ar == nil then
        print(err)
        return err, cur_di
      end
      for _,c in pairs(label_ar) do
        result = result .. string.char(c)
      end
      result = result .. "."
      cur_di = cur_di + labellen
      labellen = event:get_octet(cur_di)
      cur_di = cur_di + 1
    end
    return result, cur_di
end

function dns_skip_dname(event, i)
    --print("[XX] Get octet at " .. i)
    local labellen = event:get_octet(i)
    if bit.band(labellen, 0xc0) then
        --print("[XX] is dname shortcut")
        return i + 2
    end
    while labellen > 0 do
      i = i + 1 + labellen
--      print("Get octet at " .. i)
      labellen = event:get_octet(i)
      if bit.band(labellen, 0xc0) then
          --print("[XX] is dname shortcut")
          return i + 2
      end
    end
    return i
end

function get_dns_answer_info(event)
    -- check whether this is a dns answer event (source port 53)
    if event:get_octet(21) ~= 53 then
        return nil, "Event is not a DNS answer"
    end
    local dnsp = event:get_payload_dns()
    print(dnsp:tostring());

    -- check if there are a query and answer in the first place
    qdcount = event:get_int16(32)
    --print("qdcount: " .. qdcount)
    if qdcount ~= 1 then
        return nil, "DNS data has no question info"
    end
    --print("qdcount: " .. qdcount)
    ancount = event:get_int16(34)
    --print("ancount: " .. ancount)
    if ancount < 1 then
        return nil, "DNS data has no answer"
    end
    --print("ancount: " .. ancount)
    local dname, qtype, i, ip_address
    dname, i = get_dns_qname(event)
    qtype, i = event:get_int16(i)
    --print("Domain name: " .. dname)
    --print("Query type: " .. qtype)
    -- skip the CLASS
    i = i + 2
    -- skip the answer name
    --print("skip dname at " .. i)
    i = dns_skip_dname(event, i)
    --print("i now " .. i)
    -- skip type, class, and TTL
    i = i + 8
    -- get the rdata length
    rdata_len, i = event:get_int16(i)
    --print("[XX] rdata len: " .. rdata_len)
    if qtype == 1 then
      if rdata_len ~= 4 then
        -- TODO: if CNAME etc
        return nil, "Answer to A query is not an A record (has len " .. rdata_len .. ")"
      else
        rdata_bytes = event:get_octets(i, 4)
        ip_address = table.concat(rdata_bytes, ".")
        --print("IPv4 address: " .. table.concat(rdata_bytes, "."))
      end
    elseif qtype == 28 then
      if rdata_len ~= 16 then
        -- TODO: if CNAME etc
        return nil, "Answer to AAAA query is not an AAAA record (has len " .. rdata_len .. ")"
      else
        rdata_bytes = event:get_octets(i, 16)
        ip_address = string.format("%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                                   rdata_bytes[1],
                                   rdata_bytes[2],
                                   rdata_bytes[3],
                                   rdata_bytes[4],
                                   rdata_bytes[5],
                                   rdata_bytes[6],
                                   rdata_bytes[7],
                                   rdata_bytes[8],
                                   rdata_bytes[9],
                                   rdata_bytes[10],
                                   rdata_bytes[11],
                                   rdata_bytes[12],
                                   rdata_bytes[13],
                                   rdata_bytes[14],
                                   rdata_bytes[15],
                                   rdata_bytes[16])
        --print("IPv4 address: " .. table.concat(rdata_bytes, "."))
      end
    end

    if ip_address then
        info = {}
        info.to_addr = event:get_to_addr()
        info.timestamp = event:get_timestamp()
        info.dname = dname
        info.ip_address = ip_address
        return info
    end
    return nil, "something did not match"
end

function my_cb(mydata, event)
    print("Event:")
    print("  from: " .. event:get_from_addr())
    print("  to:   " .. event:get_to_addr())
    print("  source port: " .. event:get_octet(21))
    print("  timestamp: " .. event:get_timestamp())
    print("  size: " .. event:get_payload_size())
    print("  hex:")
    print_array(event:get_payload_hex());

    if event:get_octet(21) == 53 then
      print(get_dns_answer_info(event))
    end
end

local info_cache = {}

function print_dns_cb(mydata, event)
    if event:get_octet(21) == 53 then
      print_array(event:get_payload_hex());
      info, err = get_dns_answer_info(event)
      if info == nil then
        --print("Error: " .. err)
      else
        --print("er got " .. info.timestamp .. " " .. info.dname .. " " .. info.ip_address)
        local cached = false
        for i, cinfo in pairs(info_cache) do
          --print("[XX] cinfo: " .. cinfo.timestamp .. " " .. cinfo.dname .. " " .. cinfo.ip_address)
          if cinfo.to_addr == info.to_addr and cinfo.dname == info.dname and cinfo.ip_address == info.ip_address then
            -- cached, don't print
            --print("[XX] cached")
            cached = true
          end
        end
        if not cached then
          -- print, add to cache, cleanup cache
          print(info.timestamp .. " " .. info.to_addr .. " " .. info.dname .. " " .. info.ip_address)
          table.insert(info_cache, info)
          if table.getn(info_cache) > 10 then
            table.remove(info_cache, 0)
          end
        end
      end
    end
end

local mydata = {}
mydata.foo = 123
mydata.bar = "asdf"

--nl = lnflog.setup_netlogger_loop(1, my_cb, mydata)
nl = lnflog.setup_netlogger_loop(771, print_dns_cb, mydata, 0.1, 18000000)
--nl:loop_forever()
for i=1,200 do
    nl:loop_once()
end
nl:close()
