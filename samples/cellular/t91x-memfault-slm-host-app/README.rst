.. zephyr:code-sample:: cellular-modem
   :name: Cellular modem

   Use a cellular modem to communicate with a UDP server.

Overview
********

This sample consists of a simple application which powers on
the modem, brings up the net interface, then sends a packet
with pseudo random data to the endpoint test-endpoint.com,
which is a publicly hosted server which runs the Python
script found in the server folder. DNS is used to look
up the IP of test-endpoint.com.

Notes
*****

This sample uses the devicetree alias ``modem`` to identify
the modem instance to use. The sample also presumes that
the modem driver creates the only PPP network interface.

nRF91 updates
*************

OTA download and application are started explicitly from the shell, not at boot:

.. code-block:: shell

        ota download
        ota apply

``ota download`` checks Memfault for an available update and saves its bundle to
``/ota/bundle.bin`` without applying it. ``ota apply`` verifies and applies the
saved bundle without downloading it again. Both commands queue the operation on
a dedicated OTA thread and return immediately, leaving the shell responsive to
input and log output. Progress and completion are reported through logging.
Only one OTA operation can be queued or running; another request returns
``-EBUSY`` until it finishes.
Download requires a connected network; apply can run offline after the sample
has completed startup. Boot still initializes networking, uploads Memfault data,
and confirms the host MCUboot image.

After downloading a Memfault OTA bundle, the application verifies the complete
payload SHA-256 and element sizes before applying supported nRF91 elements:

* ``nrf91_app``: a MCUboot-signed nRF91 application binary (XDFU type 0).
* ``nrf91_mdm_delta``: a modem delta image compatible with the installed modem
        firmware (XDFU type 1).
* ``nrf91_mdm_full``: a Nordic full-modem firmware CBOR package (XDFU type 2).

Do not supply ZIP archives. Delta and full modem images are mutually exclusive
in a bundle; generate bundles using ``ota/ota-bundle.py``.

The local XDFU helper in ``src/sm_xdfu.c`` is adapted from the serial-modem
``sm_xdfu`` library. It reads a bounded range directly from ``/ota/bundle.bin``
without extracting a second file. Application and delta updates use
``AT#XDFUINIT=<type>,<size>``, chunks of at most 4096 bytes with
``AT#XDFUWRITE=<type>,<offset>,<length>``, and ``AT#XDFUAPPLY=<type>``. Each
write and apply must report a successful ``#XDFU`` completion before continuing.

For full-modem updates, the host validates the CBOR wrapper, segment ranges,
and manifest firmware-blob SHA-256 before entering bootloader mode with
``AT#XDFUINIT=2``. It waits for ``Bootloader mode ready``, writes segments to
their declared addresses, and applies the first bootloader segment separately.
The final ``AT#XDFUAPPLY=2`` applies the remaining firmware and reboots the
modem automatically; no additional reset is sent on success.

PPP is brought down and the cellular driver suspended before the helper takes
over its UART. After applying the image, the helper resets the nRF91 and waits
for ``Ready`` before releasing the UART and restoring networking. A reset GPIO
is needed to recover if a transfer fails while the modem may be in binary mode.
Network registration after restoration is asynchronous.

The nRF91 firmware must support these XDFU commands; full-modem updates also
require ``CONFIG_SM_DFU_MODEM_FULL=y`` in that firmware. A new application must
confirm its MCUboot test image after booting. The host's own MCUboot confirmation
does not confirm the nRF91 image. The nRF91 bootloader target is logged and
skipped. The bundle is retained on external flash for inspection or retry.

Full-modem updates are destructive once programming begins. If a full update
fails, the host leaves the modem suspended and does not restart PPP. Retry the
complete full-modem update from the retained bundle with ``ota apply``; normal
cellular service may be unavailable until it completes. A successful update
restores the modem driver, and networking can be brought up again afterwards.
Firmware applies elements strictly in the order stored in the bundle. The bundle
generator places full-modem firmware first, if present, and preserves the input
order of the other elements. This lets a mixed bundle retry full DFU while in
bootloader mode without firmware reordering. Regenerate existing mixed bundles
to encode that ordering before using them for full-modem recovery.

nRF53 application and network-core updates
*****************************************

``nrf53_app`` must contain the MCUboot-signed application binary, normally
``build/t91x-memfault-slm-host-app/zephyr/zephyr.signed.bin``. ``nrf53_net`` must
contain the signed network-core update image, normally
``build/signed_by_mcuboot_and_b0_empty_net_core.bin`` for this sample's NSIB
network-core configuration. Do not use raw HEX files or ZIP archives as elements.

The host stages app image pair 0 in ``slot1_partition`` and network-core image
pair 1 in ``slot3_partition``, reading each element's exact range from the bundle.
Staging and nRF91 transfers follow file order. After all supported transfers
succeed, only the nRF53 images present in the bundle are scheduled, then the
host reboots. MCUboot validates the staged images and forwards the network-core
image through PCD. App-only bundles do not schedule the network-core slot.

``sysbuild.conf`` enables ``SB_CONFIG_MCUBOOT_NRF53_MULTI_IMAGE_UPDATE``. Sysbuild
assigns two application-visible image pairs and preserves the separate MCUboot
self-update image. Rebuild and flash the updated bootloader and application
together before using network-core OTA; an application-only update cannot add
network-core support to an already installed bootloader. Regenerate bundle
payloads from the new build. Hardware activation and rollback behavior must be
tested with the corresponding bootloader and signed images.

nRF53 bootloader updates
************************

``nrf53_bl`` uses the existing MCUboot-managed self-update path. Supply the
MCUboot-wrapped, NSIB-signed image linked for the inactive bank, for example
``build/signed_by_mcuboot_and_b0_mcuboot_s1_variant.bin`` when S0 is active,
or ``build/signed_by_mcuboot_and_b0_mcuboot.bin`` when S1 is active. The NSIB
firmware version must allow the new bank to be selected on boot. MCUboot verifies
the candidate and copies it into the inactive S0/S1 bank. Neither bank is written
directly by the application, and bootloader code/protection settings are unchanged.

Application and bootloader candidates share ``slot1_partition``. The generator
prints a warning if ``nrf53_app`` and ``nrf53_bl`` are both included, but still
creates the bundle in the prescribed order. Firmware tracks whether that slot
has already been primed and returns ``-EBUSY`` before a second shared-slot write.
On this error it does not schedule updates or reboot. Use separate app and
bootloader bundles; a bootloader-plus-network-core bundle is supported because
the network-core candidate uses a different staging partition.

Setup
*****

Start by setting up the devicetree with the required
devicetree node:

.. code-block:: devicetree

   /dts-v1/;

   / {
           aliases {
                   modem = &modem;
           };
   };

   &usart2 {
           pinctrl-0 = <&usart2_tx_pa2 &usart2_rx_pa3 &usart2_rts_pa1 &usart2_cts_pa0>;
           pinctrl-names = "default";
           current-speed = <115200>;
           hw-flow-control;
           status = "okay";

           modem: modem {
                   compatible = "quectel,bg9x";
                   mdm-power-gpios = <&gpioe 2 GPIO_ACTIVE_HIGH>;
                   mdm-reset-gpios = <&gpioe 3 GPIO_ACTIVE_HIGH>;
                   status = "okay";
           };
   };

Next, the UART API must be specified using ``CONFIG_UART_INTERRUPT_DRIVEN=y`` or
``CONFIG_UART_ASYNC_API=y``. The driver doesn't support UART polling.

Lastly, the APN must be configured using ``CONFIG_MODEM_CELLULAR_APN=""``.

Shell commands
**************

The sample enables :kconfig:option:`CONFIG_MODEM_CELLULAR_SHELL`, which
adds a ``modem_cellular`` shell command for runtime control of the
cellular_modem driver's periodic chat script:

.. code-block:: shell

   modem_cellular pause <device>
   modem_cellular resume <device>

Use ``modem_cellular pause`` to suppress the periodic CSQ/CREG poll while
running a long AT operation on the command channel (firmware update,
eSIM provisioning, factory test). ``modem_cellular resume`` re-enables it;
the next periodic invocation fires immediately if a scheduled run was
skipped while paused. Pausing while already paused, or resuming while
not paused, returns ``-EINVAL``.

Server setup
************

Deploy the server on a publicly accessible host, then set the
:kconfig:option:`CONFIG_SAMPLE_CELLULAR_MODEM_ENDPOINT_HOSTNAME` variable in ``prj.conf`` to the
server's hostname.

.. code-block:: shell

   git clone --depth=1 https://github.com/zephyrproject-rtos/zephyr.git
   cd zephyr/samples/net/cellular_modem/server
   python te.py
