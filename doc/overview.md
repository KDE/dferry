# Overview

Dferry is a C++ library for sending and receiving DBus messages.
Its main design goals are to present a logical, easy to use API, and high performance -
by which I mean better or much better than libdbus.

For marshalling (i.e. serialization and deserialization), Dferry offers three APIs:
- Classic, which processes DBus type signatures at processing time (TODO link)
- Bytecode based for roughly 2x the performance (TODO link)
- Code generated for roughly 10x (TODO benchmark) the performance (TODO link)

These performance numbers are for integer-heavy structured data.
With these improvements, DBus should not be considered a slow data format anymore,
and I think the goal of ease of use has been largely achieved. But see for yourself.

%Message validation in the DBus server is probably the narrowest remaining bottleneck
in most typical uses; if necessary, it can be worked around with peer-to-peer connections,
which Dferry supports.

## Supported Platforms

Dferry works on Linux, other Unix-likes, and Windows. Linux is best supported, mainly
due to most extensive testing.

## Performance details

### Input / Output

This is not usually very important except when sending large amounts of data that are very
cheap to marshal (e.g. large byte arrays such as compressed or uncompressed images).

I/O performance is currently best on Linux because Dferry uses Linux-specific APIs.
There should, however, be no major obstacles to porting it to other OS-specific
APIs with a similar structure to Linux epoll (reactor-style I/O).

Long term, a port to proactor-style I/O (Linux uring, Windows I/O completion ports or uring,
possibly others) could further improve I/O performance.

### Marshalling

The star of the show is serialization performance. The classic API is not only arguably
nicer to use than than libdbus, but already slightly faster than libdbus - so bytecode and
code-generated methods are much faster. The bytecode pipeline is comparable to modern CPUs
decoding their instruction into a more convenient internal format for all of their real
processing; performance-wise, DBus is a very inconvenient format. The Dferry bytecode format
is less so.
Bytecode preparation also includes optimizations such as completely dropping inter-element
alignment padding if it can be statically determined to be always of length zero.
The code generator uses the same optimizations and more, such as eliding all but one message
length check for consecutive fixed-length elements. Its biggest improvement, however, comes
from eliminating dispatch overhead: the generated code does not need to look up what to do
next at every step, it just continues to execute useful code.

### Isn't DBus an XML based format?

It is not. XML is only used for interface descriptions. The actual payload is encoded in a binary
format wich is described by "type signatures". The biggest issue with that is that, naively,
parsing a DBus message requires parsing the type signature and the payload data at the same
time, which greatly increases overhead.

### Limitations and TODO items

- Bytecode and code-generated API currently do not support marshalling, only demarshalling.
  I don't foresee major difficulties for that, it just needs doing and I don't get paid for
  it (but I'm available on a freelance basis: you can change that).

- Bytecode and code-generated API currently do not support variants.

- The UTF-8 encoding of strings is not validated. I am planning to use the simdutf library to
  limit the performance impact of that. Do note that the claimed good performance is not for
  string data, so these claims stand. There will most likely also be a way to turn off 
  UTF-8 validation.
  
- Unlike libdbus, Dferry does not try to be resistant to out-of-memory (OOM) situations.
  That seems to be a decent tradeoff; besides, it is unclear how well even libdbus
  really works if a malloc fails because that requires extensive testing - not to mention
  that client code, i.e. yours, would need to bee OOM-safe as well for the whole thing
  to make much sense.
  (TODO link to blog post about the difficulty of OOM safetly)
  
- There is no C API, though adding one would not be very difficult. It has been done
  before, for example for LLVM and taglib.
  
- Dferry does not include a DBus daemon

## Integration

Dferry needs to be notified when data becomes available or a timeout occurs. For this, it can
use system APIs to run its own event loop, or it can be integrated into an existing event loop
through an interface called ForeignEventLoopIntegrator (TODO link to ForeignEventLoopIntegrator).

Dferry can also be used only for marshalling on top of libdbus by grabbing or injecting,
respectively, marshalled data out of or into %DBusMessage - this allows to increase performance
and/or convenience in an existing codebase without rewriting everything.
The required hacks are relatively minor :)

The code generator is written in Python for the sole reason that it makes cross-compilation
much easier - there is no need to compile the code generator for the build platform.

## License

LGPL or Mozilla Public License - the latter is, generally speaking, more permissive regarding
static linking against Dferry than the LGPL. If that is not good enough for you, you can contact
me and we can probably work out something. I did not create Dferry with the intention to sell
licenses, but if you insist, I won't say no.
