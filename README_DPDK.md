# Table of Contents

* [Dependencies and Prerequisites](#dependencies-and-prerequisites)
  * [Data Plane Development Kit (DPDK)](#data-plane-development-kit)
  * [mTCP](#mtcp)
  * [Other System Libraries](#system-libraries)
* [Building Node.js and Components](#building-node-js-and-components)
* [Running Node.js](#running-node-js)
* [Running Multi-Process Node.js Applications](#running-multi-process-node-js-applications)


## Dependencies and Prerequisites

### Data Plane Development Kit

Data Plane Development Kit (DPDK) is a set of libraries and drivers for fast
packet processing. While DPDK is a Linux Foundation project, it is distributed
under Open Source BSD license and is available from http://dpdk.org.

### mTCP

mTCP is a highly scalable user-level TCP stack for multicore systems. This
project is distributed under Modified BSD license and is available from
https://github.com/eunyoung14/mtcp.

### System Libraries

The following libraries are also necessary.

* **libnuma**
* **libpthread**
* **libgmp**

## Building Node.js and Components

In this Section we begin by describing how to build DPDK and mTCP libraries.
Then, we move towards Node.js runtime building.

### Building DPDK and mTCP Libraries

Download mTCP source (with libc wrapper functionality) from git:

```console
$ git clone branch libc-wrapper https://github.com/eunyoung14/mtcp.git
```
You can build libmtcp library supporting multiple network drivers such as
"dpdk" or "netmap".

**Note that this branch comes with current stable version of DPDK.**

Follow "dpdk" driver build instructions from,
https://github.com/eunyoung14/mtcp/blob/master/README.md

For "netmap" driver, start by downloading source code from,
https://github.com/luigirizzo/netmap, and
follow build instructions from,
https://github.com/eunyoung14/mtcp/blob/master/README.netmap

### Building Node.js
```console
$ git clone -b add_dpdk_support https://github.com/uttampawar/node.git
$ cd node

Building with mTCP and DPDK driver
$ ./configure \
    --with-mtcp-path=<path to mtcp install dir> \
    --with-dpdk-path=<path to dpdk install dir>

Building with mTCP and netmap driver
$ ./configure \
    --with-mtcp-path=<path to mtcp install dir>

$ make or make -j
```

## Running Node.js

In this Section, we begin with showing how to set-up DPDK driver and mTCP layer.
Then, we move towards showing how to run a Node.js application.

Note: 'root' privilege is required.

##### Step 1: Build DPDK or NETMAP Driver and Setting Up Network Interface

For details about DPDK VERSION, please refer subsection in
INSTALL GUIDES part of README.md file at
https://github.com/eunyoung14/mtcp/blob/master/README.md

For details about NETMAP VERSION, please refer README at,
https://github.com/eunyoung14/mtcp/blob/master/README.netmap

##### Step 2: mTCP Configuration Set-up:
Sample mTCP related configuration files are in <mtcp source dir>/config directory.
Note: Both arp.conf and route.conf are necessary depending on your network configuration

Here we describe mtcp.conf, arp.conf and route.conf sample files:

```console
  ############### mtcp configuration file ###############
  # The underlying I/O module you want to use.
  io = dpdk
  #io = netmap #if mtcp is interfaced with netmap driver.

  # Number of cores settings
  num_cores = 1

  # Number of memory channels per processor socket
  num_mem_ch = 4

  # User port
  port = dpdk0
  #port = <netmap configure port name>

  # Maximum concurrency per core
  max_concurrency = 4096

  # Maximum number of socket buffers per core
  max_num_buffers = 4096

  # Receive buffer size of sockets
  rcvbuf = 4096

  # Send buffer size of sockets
  sndbuf = 4096

  # TCP timeout seconds (-1 can disable the timeout check)
  tcp_timeout = 240

  # TCP timewait seconds
  tcp_timewait = 0

  # Used in multi-process configuration
  # 1 - Master mode, 0 - slave mode, undefine = single instance mode
  #multiprocess = 0

  #Prints the runtime stats from DPDK driver, you can comment it out
  stat_print = dpdk0   #Prints the runtime stats from DPDK driver
  #stat_print = <netmap interface name>
```

Next is an arp.conf sample file:

```console
  #################################################################
  # This file is to configure static arp tables.
  # Rename this file to arp.conf and set the appropriate values.
  # Please save this file as config/arp.conf. Put the config/
  # directory in the same directory where the binary lies.
  #
  # (Destination IP address/IP_prefix) (Destination MAC address)
  ARP_ENTRY 2
  10.0.0.1/32 00:00:00:00:00:01
  10.0.1.1/32 00:00:00:00:00:02
  #################################################################
```

Finally, we present a route.conf sample file:

```console
  #################################################################
  # This file is routing table example of our testbed machine
  # Copy this file to route.conf and give appropriate routes
  # Please save this file as config/route.conf. Put the config/
  # directory in the same directory where the binary lies.
  #
  # (Destination address)/(Prefix) (Device name)
  #
  #
  # Add entry for default gateway route as:
  # w.x.y.z/0 dpdk0
  # Always put the default gateway route as the last entry.
  # Make sure that the mask (Prefix) is set to 0. For example,
  # if the default gateway IP address is 10.0.0.10, then the
  # entry will be:
  # 10.0.0.10/0 dpdk0
  #
  ROUTES 1
  10.0.0.1/24 dpdk0
  #################################################################
```

Note: All mTCP configuration files should be in a config directory of the 
Node.js application, see the following example:

```console
An example of Node.js application called "hello_app", illustrating the location of
configuration files.

hello_app
|- controllers
  |- ...
|- views
  |- ...
|- models
  |- ...
|- server.js
|- config   ## <-- config directory with config files
  |- arp.conf
  |- route.conf
  |- mtcp.conf
```

##### Step 3: Running Node.js Application.

   Please check that your Node.js application listens on the DPDK interace (ip/port).

```console
Continuing with hello_app example,
# cd hello_app
# export MTCP_CONFIG=`pwd`/config/mtcp.conf
# node server.js
```

## Running Multi-Process Node.js Applications

This mode of execution is equivalent to cluster based execution,
however, without cluster module. While the first process is in multi-process
mode, we always assume that it needs to create an 'n' number of queues, where
'n' are the total number of CPUs currently online in the system. Moreover, 
some NICs allow a limited number of RSS queues to be allocated. For example,
ixgbe-specific and igb-specific adapters allow only 16 and 8 queues,
respectively. In such cases, we recommend using at most 16 or 8 CPUs, respectively, avoiding unexpected runtime failure issues.

 ```console
You can manipulate (turn on or off) core setting with following sample script,
------------------------------------------------------------------------------
#!/bin/bash
# Check if you are root
user=`whoami`
if [ "root" != "$user" ]
then
  echo "You are not root!"
  exit 1
fi

# The following example shows how to turn off all cores except 0,1,2 and 3, 
# in a system with 88 CPU cores.
for i in {87..4}
do
  echo 0 > /sys/devices/system/cpu/cpu$i/online  # Turn OFF core
  #echo 1 > /sys/devices/system/cpu/cpu$i/online  # Turn ON core
done
------------------------------------------------------------------------------
```

Continuing with the multi-process set-up/execution

```console
1) Update mtcp.conf configuration file, with
   multiprocess = 1
   see also Step 2 in [Running Node.js](#running-node-js)

2) Running a multi-process application
   Continue using hello_app example, we can do following,

Start first process which acts as a master process
# cd hello_app
# export MTCP_CONFIG=`pwd`/config/mtcp.conf
# taskset -c 0 <path to node build dir>/node server.js

Assuming you want to run total of 4 processes, first one is already running.
To start remaining processes, execute following sample script as a 'root' user.
Update this as per your need.

 #!/bin/bash
 cd hello_app
 export MTCP_CONFIG=`pwd`/config/mtcp.conf
 for i in {1..3}
 do
   export MTCP_CORE_ID=$i
   taskset -c $i <path to node build dir>/node server.js &
 done

3) To stop the application.
# pkill -SIGTERM node
```

## Troubleshooting/Help
#### 1. Unable to allocate enough number of huge pages.
You have following options:

 1) increase number of hugepages for DPDK process using dpdk-setup.sh script

or

 2) reduce concurrency, recv/send buffer limits from mtcp.conf file.

