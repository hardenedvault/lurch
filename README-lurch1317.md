# Copyright (C) 2021, HardenedVault Limited (https://hardenedvault.net)

# lurch1317
/lʊʁç/. In German, an Axolotl is a type of Lurch, which simply means 'amphibian'. This plugin brings _Axolotl_, by now renamed to _double ratchet_, to _libpurple_ applications such as [Pidgin](https://www.pidgin.im/) by implementing the [XEP-0384: OMEMO Encryption](https://xmpp.org/extensions/xep-0384.html). For a higher-level overview, see [the official OMEMO homepage](https://conversations.im/omemo/). For more info, please read [lurch's README](https://github.com/hardenedvault/lurch/blob/lurch1317/README.md). lurch1317 is an implementation of vault1317 protocol for Pidgin plugin.

## Build

```
git clone https://github.com/hardenedvault/lurch.git --recursive
cd lurch
make
```

## Installation

```
cp build/lurch1317.so ~/.purple/plugins
```
Enable the lurch1317 plugin in Pidgin:

![](/images/lurch1317-enable.png)


Check the "Help" info:

![](/images/dake-help.png)


Say "Hello" to the world via metadata protection and deniablity:

![](/images/hello_vaultworld.png)

## [Protocol analysis](https://github.com/hardenedvault/vault1317/tree/master/models)

OTRv3:

![](/images/otrv3.png)


IDAKE for online messaging

![](/images/idake.png)


ODAKE for offline messaging
![](/images/odake.png)
