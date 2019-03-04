--
--  Copyright (c) 2018 Caspar Schutijser <caspar.schutijser@sidn.nl>
-- 
--  Permission to use, copy, modify, and distribute this software for any
--  purpose with or without fee is hereby granted, provided that the above
--  copyright notice and this permission notice appear in all copies.
-- 
--  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
--  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
--  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
--  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
--  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
--  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
--  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
--

local util_validate = {}

-- Assert that specified string somewhat looks like an IP address
function util_validate.somewhat_validate_ip(ip)
	assert(string.match(ip, "^[%x%.:/]+$"),
	    "does not sufficiently look like an IP address: " .. ip)
end

-- Asserts that specified string looks like a MAC address
function util_validate.validate_mac(mac)
	assert(string.match(mac, "^%x%x:%x%x:%x%x:%x%x:%x%x:%x%x$"),
	    "invalid MAC address: " .. mac)
end

return util_validate
