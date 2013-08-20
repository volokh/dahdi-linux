Cronyx Adapter Driver Package for OS Linux version 6.0
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
This file is OUT-OF-DATE, the date of assembly and/or
revision 2006-10-05. For up-to-date information please
refer to the Russian version.


Manual Contents:
~~~~~~~~~~~~~~~~

 1. General.
  1.1. System Requirements.
  1.2. Introduction.
  1.3. Drivers Bundle Content.
   1.3.1. Loadable Communication Protocol Modules.
   1.3.2. Loadable Adapter Driver Modules.
   1.3.3. Loadable Binder Control Module.
   1.3.4. The sconfig Utility and Other Components.
 2. Installation.
  2.1. Selecting Protocol Modules.
  2.2. Selecting Adapter Model Series Support Modules.
  2.3. Other Optional Parameters.
 3. Usage.
  3.1. Adapter, Line interface and Channel Naming.
  3.2. sconfig Command Line Options.
  3.3. Valid Parameter Set.
  3.4. Brief Descriptions of Adapter Models.
   3.4.1. Tau-PCI/32 Series.
   3.4.2. Tau-PCI Series.
   3.4.3. Tau-ISA Series.
   3.4.4. Sigma-ISA Series.
  3.5. Zaptel and IP-PBX Asterisk Support.
  3.6. sconfig Usage Examples.
  3.7. Configuring /etc/cronyx.conf.

___________________________________________________________________________

1. General.
~~~~~~~~~~~

1.1. System Requirements.
~~~~~~~~~~~~~~~~~~~~~~~~~

 * Intel Pentium compatible processor or later.
 * Linux OS kernel version 2.4.x (from 2.4.28) or 2.6.x (from 2.6.9).
 * Zaptel-stack source code for Zaptel/Asterisk support.
 * Standard Linux OS environment (bash, cat, sed, tc, make, etc.).
 * GNU GCC Compiler version 3.2 or later.
 * 10 Mbytes of free disk space.
 * Responsible and judicious system administrator.


1.2. Introduction.
~~~~~~~~~~~~~~~~~~

This driver package is designed for providing various adapter
functionalities for the "Cronyx Engineering" PC-platform.

The driver package contains several Linux OS kernel source code
modules and a sconfig control utility. All user (system administrator)
and driver package interaction is performed via the sconfig utility.
Moreover, to facilitate usage under the most common configurations, a
single /etc/cronyx.conf configuration file is provided, and a
sh-script for processing it, which may be installed into the rc.d or
init.d subsystem.

The driver package includes several "protocol modules", which
implement interaction between low-level adapter drivers and the
remaining system components. Binding the "protocol modules" to
low-level drivers and interface provisioning for centralized control
are among the tasks of the `binder' module.

The control of the driver package includes loading the needed kernel
modules, setting the required parameters (for line interfaces, logical
data channels and whole adapters) and assigning channel protocols, and
configuring the resulting network interfaces.

Protocol modules with network support create usual network interfaces
in the system. Configuration and interaction with these is performed
in a common way (using ifconfig, etc.).

Development and testing of the driver package operations must be
performed under Gentoo Linux and the latest official kernels of the
2.6 line available at http://www.kernel.org.

Linux line 2.4 kernels are supported on the precedent basis, i.e.
corrections and improvements are made only in response to problem
messages and bug reports.

Linux kernels before 2.4.28, including 2.2.x and 2.0.У are not
supported and will not be supported ever.

It is recommended to set the tabulation size equal to 4, when viewing
source code.


1.3. Drivers Bundle Content.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The driver package contains several components:

1.3.1. Loadable Communication Protocol Modules*:
 a) cisco.ko - Cisco HDLC protocol driver;
 b) fr.ko - Frame Relay protocol driver;
 c) rbrg.ko - Ethernet bridge driver (see below);
 d) raw.ko and packet.ko - `transparent' mode drivers (w/o network support);
 e) async.ko - asynchronous mode drivers (w/o network support);
 f) sync.ko - synchronous tty driver for the PPP protocol;
 g) czaptel.ko - provides a Zaptel-compatible interface for
    the http://www.asterisk.org/ project, see below;

  *  - kernel version 2.6.У modules have a `.ko' extension, previous
       version modules have an `.o' extension.

It is possible to load not all protocols, but only the required ones.

Modules cisco.ko, fr.ko, and rbrg.ko implement network interfaces,
which may be controlled using the ifconfig command.

The sync.ko module serves synchronous tty devices of the
/dev/cronyx/<channel-name> for the operation of the pppd daemon.
Depending on the OS used, /dev/ttyZ# devices may be automatically
created.

For the operation of the PPP mode, the standard PPP support must be
provided by the kernel (the ppp.ko module). In the PPP the pppd daemon
must be started for working with a /dev/cronyx/<channel-name> device.
This daemon provides PPP protocol support.

The async.ko module serves asynchronous /dev/<channel-name> and
/dev/<cu_channel-name> tty devices. Depending on the OS used,
/dev/ttyQ# and /dev/cuq# devices may be automatically created. The
asynchronous mode is only supported by some models of Sigma-ISA series
adapters.

The raw.ko provides the capability to work with the channel in the
low-level mode via /dev/cronyx/<channel-name>, e.g., for implementing
custom protocols. Only the HDLC mode is available for most adapters
(HDLC Layer 2 frame transmit/receive), the transparent (aka
"telephone") mode is also available for Tau-PCI/2E1, Tau-PCI/4E1
adapters and for the Tau-PCI/32 series.

The packet.ko provides functionality similar to raw.ko, but also
provides the packet mode with buffering for transmitted data. This
allows to reduce the number of short HDLC packets, thus decreasing the
overhead.

The usage of the raw.ko and packet.ko modules suggests the development
of additional software by the customer, and this software will
exchange data with standard file operations, via
/dev/cronyx/<channel-name>.


1.3.2. Loadable Adapter Driver Modules:
 a) ce.ko - driver for Tau-PCI/32 series PCI adapters;
 b) cp.ko - driver for Tau-PCI series PCI adapters (Tau-PCI-Lite,
    Tau-PCI/2E1, Tau-PCI/4E1, etc.);
 c) ct.ko - driver for Tau-ISA series ISA adapters (discontinued);
 d) cx.ko - driver for Sigma-ISA series ISA adapters (discontinued);

These modules provide interaction with equipment and data transmission
to the corresponding protocol module for processing.


1.3.3. The loadable binder.ko control module. It binds protocol
modules and adapter driver modules together, and also implements the
/dev/cronyx/binder device for control. It also processes some common
ioctl calls.


1.3.4. The sconfig Utility and Other Components.

The sconfig configuration utilities. All adapters, protocol modules,
logical channels and interfaces control is performed via the sconfig
utility. Exceptions are kernel module loading and unloading.

The cronyx.sh command file (script) for driver loading and
initialization.

Example cronyx.conf configuration file.

___________________________________________________________________________

2. Driver Bundle Installation.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If any Cronyx adapter drivers have already been installed, then,
before installing the new version, all previous version kernel modules
must be unloaded and removed, in order to avoid possible conflicts and
ambiguities. Version 5.x driver package modules were installed into
the /lib/modules/kernel-name/kernel/net/cronyx directory, the location
for earlier versions may differ. Version 6.x driver package modules
were installed into the /lib/modules/kernel-name/cronyx directory.

The driver package is built from the supplied source code using the
standard Linux OS building method. Header files and the target OS
kernel configuration are required to compile modules. The full set of
OS kernel source code is required for kernel versions 2.4.x.

The building system must be adjusted for your environment before
compiling. The ./configure script is included for this purpose.

This script accepts a set of options, which allows to specify the
required set of modules to be used, and other parameters. It is
required to select at least a single protocol module, and a single
equipment support module. When running, the ./configure script tries
to detect (by calling the lspci utility) the presence of installed PCI
adapters, and automatically selects the corresponding modules.


2.1. To select the required set of protocol modules:

  --enable-fr      - enables the Frame Relay module (fr.ko);

  --enable-cisco   - enables the Cisco/HDLC module (cisco.ko);

  --enable-rbrg    - enables the Ethernet bridge module (rbrg.ko);

  --enable-sync    - enables synchronous tty support (sync.ko) for
                     the PPP protocol;

  --enable-raw     - enables transparent mode support for user
                     programs (raw.ko);

  --enable-packet  - enables the packet HDLC mode for user
                     programs (packet.ko);

  --enable-async   - enables the asynchronous mode support (async.ko,
                     for Sigma-ISA adapters only);

  --with-zaptel=.. - enables zaptel/asterisk.org support, and
                     specifies the location of header files for
                     interface telephony Zaptel driver. Many Linux
                     distribution kits already include a zaptel
                     interface driver package. In these cases you must
                     specify the path to the directory in the source
                     code of the installed Linux kernel containing the
                     zaptel.h file;


2.2. To select the required equipment support module set:

  --enable-ce  - enables the `ce' module for Tau-PCI/32 series adapters.
                 If the --enable-ce option is not specified, then the
                 ./configure script tries to detect the presence of
                 Tau-PCI/32;

  --enable-cp  - enables the `cp' module for Tau-PCI series adapters;
                 If the --enable-cp option is not specified, then the
                 ./configure script tries to detect the presence of
                 Tau-PCI;

  --enable-ct  - enables the `ct' module for Tau-ISA series adapters;

  --enable-cx  - enables the `cx' module for Sigma-ISA series adapters;

  --enable-all - enables support for all modules;


2.3. Other Optional Parameters:

  --cronyx-major=..  - determines the base major number for devnode
                       devices to be used by the driver package.
                       Various driver bundle components will request
                       different major numbers, starting from the
                       specified base one. As a minimum, the base
                       number itself will be used, the sync.ko uses
                       the +1 number, and the async.ko module uses the
                       +2 and +3 numbers. It is required that all
                       major numbers are free, and not used by other
                       kernel components. By default, 222 is selected
                       as a base major number.

  --with-ksrc=..     - specifies the path to configuration, header
                       files, and/or target Linux OS kernel source
                       code. Attention! The source code and
                       configuration version must fully correspond to
                       the OS kernel, under which the modules will
                       operate;

  --with-libmod=..   - specifies the path for installing built
                       modules. If not specified, an attempt will be
                       made to determine it automatically;

  --with-rcdir=..    - specifies the path for installing the cronyx.sh
                       rc-csript, for example, `/etc/init.d'. If not
                       specified, an attempt will be made to determine
                       it automatically;

  --with-rctype=..   - specifies the rc-initialization system type.
                       Valid options are `rc-update' (Gentoo),
                       `chkconfig' (RedHat), `rc.local' (Slackware).
                       If not specified, an attempt will be made to
                       determine it automatically;

  --with-rclocal=..  - specifies the path to the `rc.local' script. If
                       not specified, an attempt will be made to
                       determine it automatically;

  --with-manpath=..  - specifies the path for installing man-pages. If
                       not specified, an attempt will be made to
                       determine it automatically;

After the ./configure is successfully executed with the required
options, the driver package may be built and installed using the
following commands:
  make && make install

This must be enough when correctly installed and configured Linux
kernel source code is present. Trouble may arise when using Linux
distribution kits with a non-standard, changed external module
building process, or with some specialized versions.

In these cases we recommend you to refer to section "building &
installing external kernel modules" of you Linux OS kernel version
documentation and/or to various articles over the Internet, which
describe solutions for such problems. Please don't call us.

It should be noted, that the most common error when building the
driver package is the usage of the Linux kernel building environment,
which does not correspond (by version and/or configuration) with the
kernel used, and the usage of zaptel source code different from that
included with the kernel. In this case, during an attempt to load the
built modules, a message will be displayed about non-corresponding
signatures/versions, or the impossibility to allow external symbol
links. It is this situation that the message "kernel tained" relates
to.

___________________________________________________________________________

3. Usage.
~~~~~~~~~

3.1. Adapter, Line interface and Channel Naming.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Before version 6.0 of the package, the old "flat" naming system was
used, when names were assigned to data channels only. This scheme was
convenient and sufficient, while each logical channel was directly
corresponding to an equipment line interface.

The appearance of new generation adapters (Tau-PCI/2E1, Tau-PCI/4E1,
and Tau-PCI/32) caused confusion, since logical channels no longer
corresponded to line interfaces, and illogicality and ambiguity in
channel selection were introduced during the line interface
configuration process.

Thus, starting from version 6.0, a new "hierarchical" naming system
was introduced. The new scheme names and distinguishes several object
types: `adapter', `interface', `channel'. At the same time, each
object has its own set of configuration parameters. Accordingly, a
parameter set must be determined for each object, in order to specify
a complete configuration. At that, more operations are required, but
there always is complete clearness and unambiguity.

Adapters are assigned names in the <adapter-type_#> form, for example,
`tau32_0', `taupci_0', `tauisa_2', etc.

Line interfaces (e.g. E1, V.35, RS-530, etc.) are assigned names in
the <adapter-name.interface-type_#> form. In this form `adapter-name'
corresponds to the adapter, where the line interface is located. And
the `interface-type_#' determines interface type and sequential number
in the adapter. For example, `tau32_0.e1_0', `taupci_0.e1_3',
'tauisa_0.s_1', etc.

Line interface types are named as follows:
  `s'      - synchronous serial interface;
  `e1'     - E1 interface (ITU-T G.703) with
             ITU-T G.704 framed mode support;
  `g703'   - ITU-T G.703 interface with unframed mode support only;
  `e3'     - E3 interface;
  `rs232'  - synchronous RS-232;
  `rs449'  - synchronous RS-449;
  `rs530'  - synchronous RS-530;
  `v35'    - synchronous V.35;
  `x21'    - synchronous X.21;
  `a'      - asynchronous serial RS-232 interface;
  `u'      - multi-purpose synchronous/asynchronous interface;

Logical data channels receive names in the 'adapter-name.#' form,
where the corresponding adapter name, in which logical channel is
located, is specified along with the channel number in the adapter.
For example, `tau32_0.0', `tau32_0.31', `taupci_1.3', etc.

In addition to that, for convenience, logical channels are assigned
aliases, which are equal to their names in the old naming scheme. This
allows using shorter names (including those of the resulting network
interfaces), while simultaneously keeping the maximum compatibility
with application software designed for the old naming scheme. Logical
channel aliases have the <ce#>, <cp#>, <ct#> and <cx#> form for
Tau-PCI/32, Tau-PCI, Tau-ISA, and Sigma-ISA adapters correspondingly.

The actual available object list may be viewed anytime using the
'sconfig -r' command. Logical channel aliases are displayed with a `/'
(slash) after the name.


3.2. Sconfig Command Line Options.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

sconfig [-raimsxeftucqv] [<object-name> [parameters...]]
  -r  - print current roadmap of objects;
  -a  - display all configuration information, not only
        the most significant parameters;
  -i  - display network interface statistics;
  -m  - display modem signal state information. The description of
        all signals can be found in any document related to the modems.
        Only LE signal should be described. If this signal is On,
        than some what use channel. If it is Off, than channel is free;
  -s  - display brief logical channel statistics;
  -x  - display detailed logical channel statistics;
  -e  - display brief E1/G.703 interface statistics;
  -f  - display full E1/G.703 interface statistics;
  -t  - display brief E3 interface statistics;
  -u  - display full E3 interface statistics;
  -c  - clear statistics;
  -q  - be quiet, do not display any information;
  -v  - show version information;

If <object-name> is not specified, then the corresponding actions
will be taken for all existing objects.


3.3. Valid Parameter Set.
~~~~~~~~~~~~~~~~~~~~~~~~~

To view the current configuration, you must call the sconfig utility
with a name of the object of interest. When called without parameters,
sconfig displays the main configuration parameters for all available
objects.

To change configuration, you must supply the sconfig utility, along
with the object name, with names of the parameters to be changed, and
their values in the <parameter=value> form.

Different sets of parameters are available for adapters, interfaces
and channels. The set of available parameters in each particular case
depends on the adapter model, interface type, operating mode, and the
selected protocol. For example, the `line=' parameter (line encoding)
is not available for asynchronous interfaces, and the `dpll='
parameter (enable DPLL) is not available for E1 interfaces. To view
all parameters applicable to an object, call sconfig with the `-a'
option, specifying the object name.

The complete list of all configuration parameters:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

General adapter configuration parameters:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  adapter=.. -- specifies the operating mode for Tau-ISA and Tau-PCI
                adapters with E1 interfaces, possible values:
     separate - independent channel mode, a single logical channel
                corresponds to each line interface;
          mux - timeslot multiplexing mode between line interfaces and
                logical channels;
        split - distribution of E1 interface timeslots between logical
                transmit/receive channels;
       b-mode - legacy "B" mode for the Tau-ISA/E1 adapter;

  led=..     -- specifies the adapter LED indication mode for various
                situations and events. The following options
                combinations are allowed, which must be listed without
                spaces, separated by commas:
        smart - the default mode,
                the indicator flashes depending on the state of the
                physical interface (loop, loss of carrier, loss of
                frame, etc.);
           on - the indicator lights continuously;
           ff - the indicator is off;

   #(number)  - a 32-bit value, allowing to specify an arbitrary
                cadence mode;
          irq - if specified, the indicator momentarily lights
                (or goes off) during each hardware interrupt from the
                adapter side;
           rx - if specified, the indicator momentarily lights or (goes
                off) on data packet/chunk reception;
           tx - if specified, the indicator momentarily lights (or
                goes off) on data packet/chunk transmission;
          err - if specified, the indicator momentarily lights (or
                goes off) on transmit/receive errors;

  subchan=..     -- specifies the number of timeslots for the "B" mode
                    of the Tau-ISA/E1 adapter;

  reset          -- software adapter reset;

  hardware-reset -- adapter restart with a complete hardware reset;

Logical channel configuration parameters:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  debug=#      -- specifies the debug information display level
                  (0..2), "0" - debugging disabled, "2" - maximum
                  debug information (for developers);

  extclock     -- select the external clock mode for serial
                  synchronous interfaces (e.g. V.35, RS-530, etc.).
                  External clock mode is the most common method
                  for connecting external modem hardware (aka DCE).
                  In this mode the external timing signal is received
                  on TXCIN pin of the connector, and it is used as
                  a synchronization clock for transmitting data (TXD);

 #(number)     -- specifies the rate, and enables synchronization from
                  adapter's internal clock for serial synchronous
                  interfaces (e.g. V.35, RS-530, etc.). Zero value
                  is equal to the 'extclock' option. If case of
                  nonzero value it will cause setting data rate
                  to given value and setting the internal clock source
                  of the synchronization (in synchronous mode). The
                  transmitted data (TXD) are synchronized using the
                  internal on-board timing generator, the internally
                  generated timing signal is driven on the TXCOUT pin,
                  and the signal on the TXCIN pin is ignored. This mode
                  is used for direct connection of adapter to the
                  terminal (aka DTE), e.g. for connecting two computers
                  together in a synchronous mode via relatively short
                  cable. Also internal clock mode should also be used
                  for testing channels with an external loopback
                  connector;

  mtu=#        -- specifies the MTU (Maximum Transmission Unit)
                  size limit;

  qlen=#       -- specifies the transmit/receive queues length;

  timeslots=.. -- specifies the timeslot list for channels
                  bounded to E1 interfaces, for example: "1-5,17,19-24";

  dlci=#       -- when using the Frame Relay protocol module, adds a
                  PVC (Permanent Virtual Circuit) with a specified
                  DLCI number;

  iface=#      -- bind a logical transmit/receive channel to a
                  line interface by its sequential number on the
                  adapter (0, 1, 2, 3...);

  mode=..      -- specifies the logical channel operating mode,
                  possible values:
          async - asynchronous mode (for Sigma-ISA only);
           hdlc - synchronous mode, HDLC (Layer 2) packet
                  transmit/receive;
          phony - for Tau-PCI/32 and Tau-PCI adapters with E1
                  interfaces, direct data exchange in transparent
                  (aka "telephone") mode;

Line interface configuration parameters:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  loop=..          -- select a loopback mode, possible values:
                off - normal mode, all loops disabled;
           internal - internal loop, the data transmitted
                      into the line are received back;
             mirror - external loop, the data received from the line
                      are transmitted (mirrored) back into the line;
             remote - request to the remote side to enable
                      return for data received from our side;

  dpll=on/off      -- enables DPLL on serial synchronous interfaces
                      for clock regeneration according to the received
                      data;

  line=..          -- line code selection, possible values depend on
                      the line interface type:
                nrz - the NRZ code, for serial synchronous interfaces;
               nrzi - the NRZI code, for serial synchronous interfaces;
               hdb3 - the HDB3 code, for E1 and G.703 interfaces;
                ami - the AMI code, for E1 and G.703 interfaces;

  invclk=..        -- for serial synchronous interfaces, the clock
                      inversion mode, possible values:
         normal/off - normal mode;
            rx-only - inversion of only reception (RXC/ERC) clock,
                      supported by Tau-PCI series adapters only;
            tx-only - inversion of only transmission (TXC/ETC) clock,
                      supported by Tau-PCI series adapters only;
            both/on - inversion of both transmission and reception clock;

  higain=on/off    -- for E1 interfaces, enable receiver
                      high-gain mode. This allows increasing the
                      distance over an E1 line to 2.5 km (over a 0.6
                      mm2 section twisted pair);

  monitor=on/off   -- for E1 interfaces, enable line monitoring
                      (listening-in) mode via high-resistance resistors;

  unframed=on/off  -- for E1 interfaces, enable the unframed
                      E1 mode (without timeslot structure);

  scrambler=on/off -- for E1 interfaces, enable scrambler in
                      the E1 unframed mode;

  cas=..           -- for E1 interfaces, specified the CAS
                      (Channel Associated Signaling) processing mode,
                      possible values:
                off - no CAS-type signaling, the 16-th timeslot is
                      available for transmitting data or CCS-type
                      signaling;
                set - CAS is controlled on receive, but is
                      substituted for transmission (no need to create
                      CAS-payload);
               pass - CAS is controlled on receive, and is
                      transmitted "as is" from the corresponding
                      logical channel;
              cross - cross-switching of CAS by means of a hardware
                      cross-connector, in parallel with timeslot's
                      payload switching;

  crc4=on/off      -- for E1 interfaces, enable CRC4 multiframes;

  clock=..         -- for E1 interfaces, specified the
                      transmitter synchronization mode, and when
                      operating in the multiplexer mode - the whole
                      transmit/receive path synchronization
                      mode, possible values:
           internal - synchronization from an internal clock;
            receive - synchronization from the receiving path, by the
                      frequency recovered from the line;
               rcv0 - synchronization from the receiving path of
                      physical interface #0;
               rcv1 - synchronization from the receiving path of
                      physical interface #1;
               rcv2 - synchronization from the receiving path of
                      physical interface #2;
               rcv3 - synchronization from the receiving path of
                      physical interface #3;

Protocol selection:
~~~~~~~~~~~~~~~~~~~

  idle   -- No protocol (detach any protocol module);

  async  -- Asynchronous protocol without network support, for
            Sigma-ISA series adapters only. When a protocol is
            selected, entries for channel access are automatically
            created in /dev/*;

  sync   -- Synchronous tty interface support without direct network
            support. Allows using standard system tools (pppd) to
            implement network interaction. When a protocol is
            selected, entries for channel access are automatically
            created in /dev/*;

  cisco  -- Cisco HDLC protocol, creates a point-to-point network
            interface;

  rbrg   -- Remote Ethernet bridge protocol. A similar module or a
            compatible device must operate on the opposite side of the
            channel (Cronyx BRDG-ETV, Cronyx BRDG-ETH series
            Ethernet-bridges, PCM2L, PCM2D, E1-L series converters,
            E1-XL multiplexers). A full Ethernet bridge is formed
            together with the correspondent, and an
            Ethernet-compatible interface is created;

  fr     -- Frame Relay protocol support (ANSI T1.167 Annex D). To
            create network point-to-point interfaces, use the dlci=#
            parameter to add PVCs with required DLCI numbers;

  raw    -- Direct data exchange support for user programs. Both
            HDLC-packet exchange, and "raw data" exchange in the
            transparent (aka "telephone") mode are possible. When a
            protocol is selected, entries for channel access are
            automatically created in /dev/cronyx/*;

  packet -- Implements the transmit/receive mode for user
            programs with aggregating small data portions into HDLC
            packets, allowing to decrease overhead. When a protocol is
            selected, entries for channel access are automatically
            created in /dev/cronyx/*;

  zaptel -- implements a Zaptel-compatible interface for open IP-PBX
            Asterisk. A Zaptel-stack must be installed in order to
            build and load the module;

Before selecting a protocol, the corresponding protocol module must be
loaded. When connecting a protocol module to a logical channel,
separate parameters may be configured. In this way, for example,
almost all protocol modules set the channel operating mode
(async/hdlc/phony).

After connecting a protocol module to a logical channel, some
parameters may become unchangeable. For example, the zaptel protocol
prohibits changes to the timeslot list, many protocol modules do not
allow changing the mtu and the channel operating mode
(async/hdlc/phony). Thus it is recommended to assign the channel
protocol as the last parameter during configuration using the sconfig
utility. After specifying the protocol, only DLCI numbers must be
specified (the dlci=# parameter) when using the Frame Relay protocol
(the fr.ko module).


3.4. Brief Descriptions of Adapter Model Series.

During the last 10 years the Cronyx Engineering Company has produced
over 20 adapter models for PC platforms. Many models and model series
are discontinued. All adapters have been and continue to be
manufactured in several revisions, which have different operating
characteristics. For more complete and precise information about
technical specification, and feature descriptions of various models
and their revisions, please, refer to the documentation included with
equipment.


3.4.1. Tau-PCI/32 Series.

The Tau-PCI/32 series adapters allow implementing up to 32 independent
data channels, with a total capacity of up to 2,048 kbit/s (one full
E1 stream). Each channel is composed of E1 timeslots, and may operate
both in the HDLC mode, and in the transparent "telephone" mode.

There are two models: Tau-PCI/32 (with two E1 interfaces) and
Tau-PCI/32-Lite (with a single E1 interface). The main difference
between the "full" and "lite" models is the cross-switching
capability, and the pass-through of unused timeslots between E1
interfaces. Besides, the Tau-PCI/32-Lite model may be installed into a
Low-Profile PCI slot.

The configuration objects in the Tau-PCI/32 model series are:

The adapter as a whole - selecting the clock source and setting the
LED indicator operating mode.

E1 interfaces - selecting the framed/unframed mode, enabling the CRC4,
the CAS processing mode, line code, decreasing the rate, enabling
loops, the scrambler, the receiver high-gain mode, the E1 line
"listening-in" mode.

Logical channels - assigning timeslots, selecting the HDLC/Transparent
mode, queue lengths, binding to an E1 interface, binding a channel
protocol (a protocol module).


3.4.2. Tau-PCI Series.

The Tau-PCI series includes over 10 models. All models have a lot in
common, but also have many differences related to different line
interfaces. Many Tau-PCI series models have an expansion capability
using an extra two-channel Delta2 series board.

Tau-PCI           - models with two synchronous serial V.35/RS-232
& Tau-PCI/R         interfaces (the Tau-PCI model), or RS-530/RS-449
                    interfaces (the Tau-PCI/R model). Expansion is
                    possible using Delta2 or Delta2/R.

Tau-PCI/Lite      - a pair of models similar to Tau-PCI and Tau-PCI/R,
& Tau-PCI/R-Lite    but with a single synchronous interface.
                    Installation is allowed into a Low-Profile PCI
                    slot.

Tau-PCI/2E1       - models with two, or four E1 interfaces, which may
& Tau-PCI/4E1       operate in both the framed and the unframed mode.
                    Up to 4 logical data channels are available, which
                    are formed from E1 timeslots, and may operate both
                    in the HDLC mode, and in the transparent
                    "telephone" mode. The Tau-PCI/2E1 model allows
                    expansion using Delta2 or Delta2/R. In this case
                    channels 2 and 3 are rigidly assigned to expansion
                    board interfaces.

Tau-PCI/E3        - a model with a single E3 34.368 Mbit/s interface,
                    with a single logical channel corresponding to it.

Tau-104           - a two-channel adapter for industrial applications
                    with a PC104+ bus (a PCI-compatible PC104 bus
                    extension). The channels are equipped with
                    multi-purpose V.35/RS-232/RS-530/X.21/V.10/V.11
                    interfaces. Interfaces are switched automatically
                    depending on the type of the cable connected.

Tau-PCI/G703      - a discontinued model with two E1 interfaces, which
                    support the unframed mode only. Expansion is
                    possible using Delta2 or Delta2/R.

Tau-PCI/E1        - a discontinued model with two E1 interfaces, which
                    support the framed mode only. Expansion is
                    possible using Delta2 or Delta2/R.


The configuration objects in the Tau-PCI model series are:

The adapter as a whole - setting the LED indicator operating mode,
selecting the mux/separate mode for Tau-PCI/2E1 and Tau-PCI/4E1
models.

E1 interfaces - selecting the framed/unframed mode, enabling CRC4, CAS
processing mode, decreasing the rate, enabling loops, the scrambler,
the receiver high-gain mode, the E1 line "listening-in" mode.

Serial interfaces - selecting encoding, loop connection interfaces,
enabling the DPLL, the clock inversion.

Logical channels - depending on the interface type, to which the
logical channel corresponds (is bound to).

In case of E1 interfaces: assigning timeslots, selecting the
HDLC/Transparent mode, binding to an E1 interface.

In case of synchronous serial interfaces: transmit/receive rate
and the clock mode.

Independently on the physical interface type: queue lengths, channel
protocol (protocol module) binding.


3.4.3. Tau-ISA Series.

The Tau-ISA series was a predecessor to the Tau-PCI series, which is
reflected in its name. Currently all Tau-ISA series adapters are
discontinued. The following adapted models were produced:

Tau-ISA       - models with two synchronous serial Tau-ISA/R
                interfaces from the V.35/RS-232 and RS-530/RS449 set,
                and Tau-ISA/VR

Tau-ISA/G703  - a model with two E1/G.703 interfaces, which support
                the unframed mode only.

Tau-ISA/E1    - models with two E1 interfaces and a single
                Tau-ISA/E1/R extra synchronous serial interface from
                the V.35/RS-232 and RS-530/RS449 set.


3.4.4. Sigma-ISA Series.

The Sigma-ISA model series adapters are among the first synchronous
serial adapters manufactured in Russia. This model series enjoyed wide
distribution during the first stages of Internet development in
Russia.

The following Sigma-ISA series models exist:

Sigma-800  - a discontinued model, equipped with eight multi-purpose
             synchronous/asynchronous RS-232 interfaces supporting
             data rates up to 128 kbit/s and full modem control.

Sigma-22   - an adapter with two synchronous serial interfaces from
             the V.35/RS-232 and RS-530/RS449 set, supporting data
             rates up to 128 kbit/s.

Sigma-24   - a Sigma-22 model adapter, provided with a Delta2
             two-channel expansion board.


3.5. Zaptel and IP-PBX Asterisk Support.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Zaptel IP-PBX Asterisk interaction interface is available for
Tau-PCI/32, Tau-PCI/32-Lite, Tau-PCI/2E1, and Tau-PCI/4E1, Tau-PCI/E1
adapters. The Tau-PCI/G.703, Tau-ISA/E1, and Tau-ISA/G.703
modification adapters are not supported (and will not ever be).

An important aspect in configuring zaptel channels is setting optimal
transmit/receive queue lengths using the qlen=# parameters. The more
the specified value, the more will be the delay introduced by the
queues, and the more the related telephone echo effect. The minimum
value for the qlen equals 2, but in this case it is required that the
system as a whole be capable to process hardware interrupts arriving
at a rate of at least 1000 per second.

All Linux kernel versions 2.6, in appropriate configurations, can
perform this task easily. Problems arise only when using device
drivers, which allow interrupt blocking for a long time (exceeding
100us). To obtain guaranteed minimum interrupt processing latency,
and, accordingly, quality zaptel/asterisk operations, it is
recommended to use real-time Linux kernels, see
http://people.redhat.com/~mingo/realtime-preempt/

To minimize problems related to setting transmit/receive queue
lengths, the czaptel.ko module implements automatic increase of queue
lengths in underrun/overrun situations.

In the new driver package version, the czaptel.ko allows using both
the full E1 stream and a subset of timeslots for telephony. Besides,
the zaptel-stack is supplied with information about E1 alarms and
error counters. The new czaptel.ko module controls the correspondence
of CAS/CCS settings between the zaptel-stack and the channel adapter.
The CRC4 and HDB3/AMI modes are set according to the zaptel
configuration.

After enabling the zaptel protocol over a logical channel, it is not
possible to change the timeslot list and the CAS/CCS processing mode.
So the CAS mode setting (cas=off/pass/cross) and timeslot selection
(ts=..) must be performed before assigning the channel
zaptel-protocol. When using /etc/cronyx.conf, this is provided
automatically.

When using CAS-type signaling (E&M, MFC-R2, R2, R1.5) you must take
into account, that the 16-th timeslot is no longer included into the
set of voice zaptel-channels. In other words, the 16-th timeslot does
not have to be skipped in the chan_unicall settings, since is does not
get into the set available to the zaptel-stack.

Due to bugs in the zttool utility, it may not be correctly used for
monitoring. It is easy to see that the 26-th and the 32-nd timeslots
are omitted from the timeslot list, and there are also other bugs.
When it is needed to use zttool, it is recommended to apply our patch
from the zttool.cronyx-patch file (patch zttool.c <
zttool.cronyx-patch) to its source code.

The DACS control (direct timeslot-based connection switching) is not
implemented in this version. Presumably, this functionality will be
implemented in version 6.1.


3.6. sconfig Usage Examples.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Viewing the current configuration.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To view the adapter/interface/channel configuration:
    sconfig [-a] [object-name]

  For example:
    sconfig sconfig -a sconfig -a tau32_0 sconfig ce0


To view the equipment, name, and adapter/interface/channel alias "map":
   sconfig -r

Viewing interface states, error counters, statistics, etc.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To view statistics/adapter/interface/channel states, see the
description of options in section 2:
    sconfig {-sixmefut} [object-name]

  For example:
    sconfig -s ce0 sconfig -m


To clear/reset statistics/counters:
    sconfig -c [object-name]

  For example:
    sconfig -c tau32_0.e1_0


Configuring adapters, line interfaces and logical channels.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Controlling adapter Tau-PCI/32 #0, turn on the LED, and set the clock
source from the E1-interface #1 receiver:
    sconfig tau32_0 led=on clock=rcv1

Controlling E1-interface #1 in adapter Tau-PCI/32 #0, enable the
framed mode, CRC4 multiframes, and disable CAS:
    sconfig tau32_0.e1_1 unframed=off crc4=on cas=off

Controlling channel #10 on adapter Tau-PCI/32, assign timeslots on
E1-interface #1, enable the Ethernet bridge protocol module:
    sconfig tau32_0.10 ts=4-8 iface=1 rbrg

Controlling channel #1 on the Tau-PCI/xE1 adapter, select the timeslot
set, and enable the zaptel-protocol, set the transmission queue length
equal to 2:
    sconfig cp1 ts=1-31 mode=phony qlen=2 zaptel


3.7. Configuring /etc/cronyx.conf.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To facilitate operations, the `/etc/cronyx.conf' configuration file is
provided, along with a sh-script for its processing.

After specifying the required configuration, it is possible to load
drivers, and apply settings using the `cronyx.start' command. Channels
may be stopped and drivers unloaded using the `cronyx.stop' command.

During installation, the `cronyx.sh' sh-script (included with the
driver package) will be installed as one of the system initialization
commands. This script will also be executed when calling the
'cronyx.start' and `cronyx.stop' commands.

Depending on the execution mode, cronyx.sh performs the analysis of
configuration parameters specified in `/etc/cronyx.conf', and
translates them into the corresponding `sconfig', `ifconfig', etc.
utility calls. It also loads (or unloads) kernel modules from the
driver package.

It is important to understand that the `/etc/cronyx.conf' file, and
the `cronyx.sh' sh-script for its processing are designed for the most
common, simplest configurations. In case the included capabilities are
not enough, the `sconfig' utility must be used.

The configuration in the `/etc/cronyx.conf' file is specified in the
form of <object-name=value> pairs, in the sh syntax. Where
<object-name> must correspond to adapter, hardware interface of
channel name. And <value> specifies configuration as a parameter list
in the sconfig utility syntax. In the channel configuration, the local
and/or remote IP-addresses or the local MAC-address must be specified
for some protocols. DLCI numbers for Frame Relay (the dlci=#
parameter) must be specified after protocol selection, and the
corresponding local and remote IP-addresses - after each DLCI.

ATTENTION!!!
~~~~~~~~~~~~
Since interface and channel names may contain the `.' symbol, and due
to limitations of the sh language syntax, in the '/etc/cronyx.conf'
file, the `.' (dot) must be replaced with an `_' (underscore).

Edit the `/etc/cronyx.conf' according to your needs. The comments
contain typical configuration examples. The list of possible
parameters and their values are given in the sconfig utility
description, see above and `man sconfig'.

___________________________________________________________________________

The latest driver versions are available from the http://www.cronyx.ru
server. Question and suggestions are welcome at info@cronyx.ru

___________________________________________________________________________
Copyright (C) 1998-2006 Cronyx Engineering
