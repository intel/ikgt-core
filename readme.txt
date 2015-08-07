###############################################################################
# Copyright (c) 2015 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
###############################################################################

This readme covers instructions for building and launching iKGT framework and
example usage (integrity) components.

=============================================================================
Release Notes
=============================================================================

Release v1.0

(1) The current release v1.0 only supports monitoring of CR0, CR4 and limited
    set of MSRs. For details of which specific bits in CR0/CR4 and which MSRs
    are supported, refer to cr0_bits, cr4_bits, and msr_regs in driver/cr0.c,
    driver/cr4.c, and driver/msr.c respectively.

(2) Monitoring of certain bits in CR0 and CR4 may cause system instability
    on certain platforms. For example, CR4.SMAP bit is supported only on
    5th generation of Core i processors and enabling of the SMAP bit on
    system that has older version of the Core i processors may hang the
    system.

Release v1.0.1

(1) Added fix for CoreOS.

Release v1.1 (this release)

(1) Support for booting iKGT with TXT/tboot
(2) Hypercall interface and in/out buffer security enhancemts
(3) Log message enhancements and python script for instruction address
    attribution

=============================================================================
Content of the Source Package
=============================================================================

   ikgt/
   ├───readme.txt          /* this file */
   ├───xmon/
   │   ├───Makefile
   │   ├───common/         /* common header files */
   │   │   └───include/
   │   │       └───...
   │   ├───core/           /* generic xmon core */
   │   │   ├───common/
   │   │   │   └───...
   │   │   ├───include/
   │   │   │   └───...
   │   │   ├───vmexit/
   │   │   │   └───...
   │   │   ├───vmx/
   │   │   │   └───...
   │   │   └───...
   │   ├───api/            /* xmon API */
   │   │   └───...
   │   ├───package/        /* ikgt install/uninstall scripts */
   │   │   └───...
   │   ├───loader/         /* pre-os xmon loader */
   │   │   └───...
   │   └───plugins/        /* xmon-plugin for supporting integrity use case */
   │       └───ikgt-plugin
   │           └───...
   └───example-usage/
       └───integrity/
           ├───Makefile
           ├───policy/     /* example policy (.json) and install script */
           │   └───...
           ├───driver/     /* example driver to configure policy        */
           │   └───...
           └───handler/    /* example vmx-root policy plugin module     */
               └───...

=============================================================================
Build environment
=============================================================================

Ubuntu 12.04/14.04 64-bit
Gcc version 4.6.3 (Ubuntu/Linaro 4.6.3-1ubuntu5)

=============================================================================
Target machine Configuration
=============================================================================

The target machine is where iKGT is to be installed.

(1) System Configuration

    - Intel Core i processors (3rd generation or newer)
    - OS: Linux (e.g. Ubuntu)
    - System memory: at least 2GB

    Note: For target machines with less than 2GB RAM, xmon launching may fail.
          Please refer to "Customization" section for more details.

=============================================================================
Building iKGT binaries
=============================================================================

The iKGT source can be obtained either from the download page of 01.org site
or from the github source code repository. If you have downloaded the source
package (i.e. ikgtsrcpkg.tar.gz) from 01.org page, you can skip step (1)
below and start from step (2).

On a Linux build machine,

(0) One-time set up for new Linux system

    Get repo tool (make sure your proxy is setup correctly)
    $ mkdir ~/bin
    $ PATH=~/bin:$PATH
    $ curl https://storage.googleapis.com/git-repo-downloads/repo > ~/bin/repo
    $ chmod a+x ~/bin/repo

    Install git
    $ sudo apt-get install git

(1) Clone the ikgt source files into a working directory (e.g. project) from
    GitHub
   $ mkdir project
   $ cd project
   $ repo init -u https://github.com/01org/ikgt-manifest.git
   $ repo sync

(2) Build ikgt binaries:
   $ cd ikgt/example-usage/integrity
   $ make
   Or
   $ make debug=1

 Output files can be found in the directories shown below:

    ikgt/xmon/bin/linux/release/ikgt_pkg.bin  - contains xmon loader,
                                                xmon and policy handler
    ikgt/example-usage/integrity/driver/ikgt_agent.ko - sample driver


=============================================================================
Installing/running iKGT
=============================================================================

Currently, xmon can be launched by the target system which uses GRUB loader
(e.g. Ubuntu). To install xmon,

(1) Type below commands:
   $ cd ikgt/xmon
   $ sudo make install
   or
   $ sudo make install debug=1

This step will copy the ikgt binaries to /boot folder and update the GRUB
configuration file for xmon boot.

(2) Enable the GRUB menu countdown display so that it gives chance
    during boot to select boot entry.

   $ sudo vi /etc/default/grub

   Comment the following two lines with #
        GRUB_HIDDEN_TIMEOUT=0
        GRUB_HIDDEN_TIMEOUT_QUIET=true

   Modify the timeout value if necessary.
        GRUB_TIMEOUT=20

   Save the changes and quit.

   $ sudo update-grub

This step is needed only when you want to have the chance to select the boot
entry on the GRUB menu during the boot.

(3) Reboot the system
   $ sudo reboot

Linux should come up with xmon running under it in vmx-root

=============================================================================
Verifying if iKGT is up and running
=============================================================================

Check cpuid and verify Intel VT-x is being used.

    $ ikgt/xmon/package/check_vtx.sh

    If it returns "VTx is not available", it implies the processor VT-x 
    feature is being used and this is expected when xmon is running.

Check if xmon is running by executing the following utility:

    $ cd ikgt/xmon/package/check_ikgt
    $ make
    $ ./check_ikgt

    If xmon is running, the utility will print out "iKGT is running".
    Xmon runs silently under the existing OS de-privileging it.

=============================================================================
Uninstalling iKGT
=============================================================================
On the target machine,

    $ cd ikgt/xmon
    $ sudo make uninstall

   This command will delete "ikgt_pkg.bin" in /boot and "20_linux_xmon"
   in /etc/grub.d, restore the changes to grub configuration files.

=============================================================================
Setting up for TXT/tboot & iKGT Boot-up
=============================================================================
iKGT can be booted from tboot. Please follow the instruction below to install
tboot and iKGT.

(1) Hardware Setup

    - Intel Core i processors.
    - OS: Ubuntu 14.04/CentOS 7
    - Ensure the following options are enabled in the BIOS menu:

      TXT, VMX, HyperThread, TPM, and All CPU Cores

(2) If iKGT has been installed previously, un-install in by following the
    "Uninstalling iKGT"

(3) Getting packages & tboot sources

    Note: DO not install tboot by 'apt-get install tboot' command as the
          tboot installed by this way is 1.8.2, which has a bug to work with
          ikgt.

    a) Install dependencies

    $ sudo apt-get install tpm-tools openssl libssl-dev libtspi-dev

       Note: the above command is for Ubuntu system. For Cent OS, use yum.

    b) If installation of tpm-tools throws an error in trousers script then
       edit /etc/init.d/trousers file else jump to step d:

     log_daemon_msg "Starting $DESC" "$NAME"
                if [ ! -e /dev/tpm* ]
                then
                        log_warning_msg "device driver not loaded, skipping."
                        exit 0
                fi
                chown tss:tss /dev/tpm*
                chown -R tss:tss /var/lib/tpm
                start-stop-daemon --start --quiet --oknodo --pidfile /var/run/${NAME}.pid --user ${USER} --chuid ${USER} --exec ${DAEMON} -- ${DAEMON_OPTS}
                RETVAL="$?"
                log_end_msg $RETVAL
                [ "$RETVAL" = 0 ] && pidof $DAEMON > /var/run/${NAME}.pid
                exit $RETVAL
                ;;

    c) Remove the highlighted string in the file. Save and exit. Then
       execute,

    $ /etc/init.d/trousers start

    d) Get latest tboot code (1.8.3):

    $ hg clone http://hg.code.sf.net/p/tboot/code tboot-code

(4) Building tboot

    $ cd tboot-code
    $ make

(5) Installing tboot

    $ sudo make install

(6) Getting the right SINIT

    a) Check status

    $ sudo txt-stat

    You can find device ID in following format:
    DIDVID: 0x0000001fa0008086
            vendor_id: 0x8086
            device_id: 0xa000
            revision_id: 0x1f

    b) Download the ACM from https://software.intel.com/en-us/articles/intel-trusted-execution-technology/ according to the device ID you got in last step.i

    c) Unzip and copy the ACM to /boot. You may need to test more than one
       latest sinit bin file to get the right one.

    d) Update grub.

    $ sudo update-grub

(7) Testing TXT Boot

    a) Reboot the system. Select tboot in grub. Type below command to check
       if tboot is launched.

    $ sudo txt-stat | less

    You should find following words to indicate tboot was launched
    successfully:

    *************************************************
    TXT measured launch: TRUE
    secrets flag set: TRUE
    *************************************************

(8) Installing iKGT

Follow the steps "Installing/running iKGT" to install iKGT.

(9) Booting with tboot and iKGT

Reboot the system. In GRUB menu, choose the boot entry with tboot and iKGT.

(10) Verifying tboot and iKGT

Use below commands to verify tboot and iKGT boot-up were successful:

    $ sudo txt-stat | less
    $ ./check_ikgt

=============================================================================
Setting up configfs and installing ikgt_agent.ko
=============================================================================

Follow these steps to set up the configfs file system under /config

(1) $ sudo mkdir /config

If configfs driver is not installed:
    (2) sudo insmod \
        /lib/modules/<installed-kernel-version>/kernel/fs/configfs/configfs.ko

(3) $ sudo mount -t configfs none /config

(4) $ sudo insmod ikgt_agent.ko

After successful installation, the driver will create /config/ikgt_agent as its
configuration space. The resource to be monitored and policy actions can now
be specified by creating directories and files in this space.

==============================================================================
Installing example policy
==============================================================================
Use python script to parse policy file and create configfs directories.
The python script is under ikgt/example-usage/integrity/policy/parse_policy.py.

sudo python parse_policy.py -f <policy_file> -b <base_dir>
where
   <policy_file> is the JSON file,
   <base_dir> is the base directory under which the resource directories
              are to be created

    $ sudo python parse_policy.py -f policy.json -b /config/ikgt_agent

You can check the new entries in configfs by executing following command
    $ tree /config

Above command should create directories and files based on the contents
of .json file. The example policy enables monitoring of following resources
with actions as shown below:
		CR0:WP  -  LOG & ALLOW
		CR0:PG  -  LOG & SKIP
		CR4:PAE -  LOG & SKIP
		MSR:EFER - LOG & SKIP

=============================================================================
Testing policy enforcement
=============================================================================

After the policy is successfully installed, all attempts to modify monitored
resources will be controlled as per the actions specified against them.

For example, if the OS tries to modify CR0:WP, the event will be logged
but will be allowed. Similarly, if the OS tries to modify EFER, the event will
be logged and the violating instruction will be skipped.

The contents of the log can be seen in /config/ikgt-agent/log/log.txt

$cat /config/ikgt_agent/log/log.txt

            cpu=0, sequence-number=19, resource-name=CR0, access=write,
            value=0x80050033, RIP=0x81055074, action=LOG_SKIP

You can use the python script, parse_log.py, to get a more descriptive
output of each event log entry.

$ sudo python parse_log.py /config/ikgt-agent/log/log.txt <output_log_file>

Example output:
     cpu=0, sequence-number=3, resource-name=msr[0xc0000080], access=write,
     value=0x00000d00ffffffff, RIP=0xffffffffc087b9f4, action=LOG_SKIP,
     caller_type=module, module-name=ikgt_test

For detailed instructions on how to set up configfs space and how to interpret
log entries, please refer to iKGT-user-guide.

=============================================================================
Customization
=============================================================================

XMON Loading Address
--------------------

Currently the loading address for xmon loader is located at 0x10000000.
The address may not available for some systems. To avoid unexpected behavior,
verify if this address is available or not on your system as following.

(1) Boot to GRUB menu

(2) Press 'c' to grub command line when GRUB boot menu appears

(3) Type the following command:

   # lsmmap

(4) If 0x10000000 is not within an available RAM range, change this
    hardcoded value in xmon/loader/pre_os/build_xmon_pkg_linux.sh to an
    address that within any of the "available RAM" regions from lsmmap command.

    Note: whenever ikgt/loader/pre_os/build_xmon_pkg_linux.sh is modified,
          it is required to rebuild loader by rebuilding ikgt_pkg.bin.

XMON Memory Size
----------------

The memory size used by XMON is hard-coded by xmon_mem_size to 6 MB. This
works for typical desktops with 4-8 GB of RAM and 2-4 CPUs. If you wish to
change this, please modify the value defined in
pre_os/build_xmon_pkg_linux.sh build script.

This can be adjusted upwards or downwards to accommodate different systems
and usages. For example systems with large amount of RAM and CPUs will need
more memory. For usages in embedded systems the value can similarly be
adjusted downwards. Target system launch can fail due to lack of memory.

=============================================================================
Known problems
=============================================================================

(1) Dependency on *.h may not always be checked.
    Use "make clean" before rebuilding after modification of any
    header file.

(2) Shutdown or reboot will lead to system hanging, if system was booted
    with tboot and iKGT. The issue will be fixed in future release.

End of file

