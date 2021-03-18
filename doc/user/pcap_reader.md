# Using the SPIN PCAP reader

Usually,
the SPIN daemon directly hooks into the (Linux) kernel to gather information
about activity on the network.
With the PCAP reader,
it becomes possible to feed the contents of a PCAP file to the SPIN daemon.

## Instructions

### Building SPIN

Build SPIN using the normal build procedure,
with one exception:
make sure to pass the `--enable-spin-pcap-reader` flag to `./configure`,
for instance as follows:
```
$ ./configure --enable-spin-pcap-reader
```

### Running `spind`

As mentioned earlier,
usually `spind` directly hooks into the kernel to gather information about
activity on the network.
Strictly speaking,
this is not necessary (or even undesirable) if you intend to feed a PCAP
to `spind`.
You can disable that behavior by starting
`spind` in *passive mode*;
do that by specifying the `-P` flag.
As a result,
you don't have to run `spind` as root.
When `spind` does not run as root,
the default socket paths must be adjusted.
That can be done by specifying the `-e` and `-j` flags.

With the above in mind,
you could start `spind` like this:
```
$ ./src/build/spind/spind -doP -e /tmp/spin-extsrc.sock -j /tmp/spin-rpc.sock
```

### Running `spin-pcap-reader`

Once `spind` is running,
you can feed a PCAP to `spind` as follows:

```
$ cd src/build/tools/spin-pcap-reader
$ ./spin-pcap-reader -e /tmp/spin-extsrc.sock -r /path/to/file.pcap
```

By default,
`spin-pcap-reader` attempts to replay the PCAP at the recorded speed
rather than replaying the PCAP as fast as possible.
If you want to disable this behavior,
specify the `-R` flag.

`spin-pcap-reader` is also able to listen to a network interface
(instead of reading a PCAP file).
This is useful on systems that do not support the Linux kernel APIs
required to gather information about network activity,
but of course it can be used on Linux systems as well.
Use `-i` to specify the network interface.

## Caveats

The SPIN PCAP reader is not perfect.
For instance:
 * Determining which devices are devices on the local network
   is not yet implemented properly.
 * It would be nice to implement sandboxing for other platforms
   besides OpenBSD.

