## Installation on existing OpenWRT router

You can install spin on an existing OpenWRT installation.

At this moment this requires a few additional steps.

### Add the custom package feed

Log into your openWRT router with ssh, and add the following line to `/etc/opkg/customfeeds.conf`:

    src/gz	sidn	https://valibox.sidnlabs.nl/downloads/packages/snapshots/<architecture>/sidn

Architecture depends on your specific router model, you can see which one you need in `/etc/opkg/distfeeds.conf`.

Add the SIDN feed key to opkg:

    cd /tmp
    wget https://valibox.sidnlabs.nl/downloads/packages/sidn_public.key
    opkg-key add sidn_public.key

Update the package feeds:

    opkg update

Install spin and its dependencies:

    opkg install spin

SPIN is now installed, but in order to use the front-end part, there are a few additional steps:

Configure mosquitto to use websockets. Add the following lines to /etc/mosquitto/mosquitto.conf (if your internal IP address is different, modify as necessary):

    port 1883 127.0.0.1 192.168.1.1

    listener 1884 192.168.1.1
    protocol websockets

Configure the web server to serve the static pages and reverse proxy, we currently have example files for nginx only, but intend to add lighttpd config as well:

If you are running nginx, copy the following sections into the relevant server settings (the local network part):

	location /spin {
	    root /www;
	    index index.html;
	}
	location /spin_graph {
	    alias /usr/lib/spin/web_ui/static/spin_api;
	    index graph.html;
	}

	location /spin_api {
	    proxy_set_header        Host $host;
	    proxy_set_header        X-Real-IP $remote_addr;
	    proxy_set_header        X-Forwarded-For $proxy_add_x_forwarded_for;
	    proxy_set_header        X-Forwarded-Proto $scheme;
	    proxy_http_version      1.1;
	    proxy_pass_request_headers      on;
	    proxy_set_header Upgrade $http_upgrade;
	    proxy_set_header Connection 'upgrade';

	    # Fix the “It appears that your reverse proxy set up is broken" error.
	    proxy_pass          http://localhost:8002;
	    proxy_read_timeout  90;
	}

Start or restart the spin daemon and webui:

    /etc/init.d/spin restart
    /etc/init.d/spin_webui restart


**TODO IN DOCUMENTATION:**

- better way to configure nginx? the default nginx config does not have something akin to 'include /etc/nginx/conf.d/*.conf'.
- what to do when (only) uhttpd is installed and no webserver. can uhttpd be reverse proxy too?
- lighttpd example
