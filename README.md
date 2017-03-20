This repository contains the implementation of Stochastic Adapative Forwarding (SAF) for ndnSIM version 2.1.

The original SAF implementation can be found at: https://github.com/danposch/saf

The code is provided under the GNU General Public License Version 3.

Prerequisites
=============

Custom version of NS-3 and specified version of ndnSIM needs to be installed.

The code should also work with the latest version of ndnSIM, but it is not guaranteed.

    # Checkout latest version of ndnSIM
    mdkir persistent-interests
    cd persistent-interests
    git clone https://github.com/named-data-ndnSIM/ns-3-dev.git ns-3
    git clone https://github.com/named-data-ndnSIM/pybindgen.git pybindgen
    git clone --recursive https://github.com/named-data-ndnSIM/ndnSIM.git ns-3/src/ndnSIM

    # Set correct version for ndnSIM and compile it
    cd ns-3
    git checkout 2c66f4c
    cd src/ndnSIM/
    git checkout a9d889b
    cd NFD/
    git checkout 85d60eb
    cd ../ndn-cxx/
    git checkout 787be41
    cd ../../../
    ./waf configure -d optimized
    ./waf
    sudo ./waf install


Compiling
=========

To configure in optimized mode without logging **(default)**:

    ./waf configure

To configure in optimized mode with scenario logging enabled (logging in NS-3 and ndnSIM modules will still be disabled,
but you can see output from NS_LOG* calls from your scenarios and extensions):

    ./waf configure --logging

To configure in debug mode with all logging enabled

    ./waf configure --debug

If you have installed NS-3 in a non-standard location, you may need to set up ``PKG_CONFIG_PATH`` variable.

Running
=======

In addition to SAF this repository contains a sample scenario to test the functionality of SAF.

    ./waf --run=simple-saf --vis

Debug Information about SAF can be printed with the option `NS_LOG=nfd.SAF`:


    NS_LOG=nfd.SAF ./waf --run=simple-saf --vis