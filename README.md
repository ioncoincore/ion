Ion Core integration/staging repository
=======================================
[ ![pipelines](https://img.shields.io/bitbucket/pipelines/ioncoin/ion/master "pipelines") ](https://bitbucket.org/ioncoin/ion/addon/pipelines/home/ "pipelines") [![Snap Status](https://build.snapcraft.io/badge/ioncoincore/ion.svg)](https://build.snapcraft.io/user/ioncoincore/ion) [![CircleCI Build Status](https://img.shields.io/circleci/build/bitbucket/ioncoin/ion/master?style=plastic&token=88c8747ed5aaec6a61964b24b8bce84906fd6867)](https://circleci.com/bb/ioncoin/ion)
[![BitBucket issues](https://img.shields.io/bitbucket/issues/ioncoin/ion.svg?style=social)](https://bitbucket.org/ioncoin/ion/issues?status=new&status=open)

**Latest release** | **Contact us/Chat with us** | **ionomy.com homepage status** |
 :-: | :-: | :-: |
[![Latest release](https://img.shields.io/github/release/ioncoincore/ion.svg)](https://bitbucket.org/ioncoin/ion/releases/latest) ![Release date](https://img.shields.io/github/release-date/ioncoincore/ion.svg) | [![Twitter](https://img.shields.io/twitter/url/https/bitbucket.org/ioncoin/ion.svg?style=social)](https://twitter.com/intent/tweet?text=Wow:&url=https%3A%2F%2Fgithub.com%2Fioncoincore%2Fion)  | [![](https://img.shields.io/website/https/ionomy.com.svg?down_color=darkred&down_message=ionomy.com%20is%20offline&style=for-the-badge&up_color=green&up_message=ionomy.com%20is%20online)](https://ionomy.com)

_If you want to encrypt your email/files, please use public pgp key provided below._
![Keybase PGP](https://img.shields.io/keybase/pgp/ioncoincore.svg?style=flat)

## Installation methods

  We offer different installation and distribution methods covering most user needs.
  Below you can see

#### Install via SNAP (_preffered method for linux users_)

_For full information about usage and possible installation methods and Branches, please check out [Ioncore Snap Builds](snap/README.md#ioncore-snap-builds)_

    sudo snap install ioncore

[![Get it from the Snap Store](https://snapcraft.io/static/images/badges/en/snap-store-black.svg)](https://snapcraft.io/ioncore)

#### Download and install latest stable release binaries (_preffered method for windows and mac os users_)

[![Latest release](https://img.shields.io/github/release/ioncoincore/ion.svg)](https://bitbucket.org/ioncoin/ion/releases/latest) ![Latest stable Release](https://img.shields.io/github/downloads/ioncoincore/ion/latest/total.svg?style=social)

_For full information about usage and possible installation methods and Branches, please check out [Build and install ion (all OS)](../../tree/master/doc)_

#### Build, compile and install from source (_for advanced users_)

- [Build, compile and install documentation](../../tree/master/doc#building)


## Ion sources:

We forked from [PIVX](https://github.com/PIVX-Project/PIVX) and integrated ION's specific features into the PivX codebase.

By doing so, we connect to an enthusiastic and active community - leaving behind old Ion code that inherits from less actively developed and maintained code. Our main sources are now derived from:

pivx | dash | bitcoin
:-: | :--: | :-: |
[![](https://github.com/PIVX-Project/PIVX/raw/master/share/pixmaps/pivx64.png)](https://github.com/PIVX-Project/PIVX) | [![](https://github.com/dashpay/dash/raw/master/share/pixmaps/dash32.png)](https://github.com/dashpay/dash) | [![](https://github.com/bitcoin/bitcoin/raw/master/share/pixmaps/bitcoin32.png)](https://github.com/bitcoin/bitcoin)

More information at [ionomy.com](https://www.ionomy.com) Visit our ANN thread at [BitcoinTalk](https://bitcointalk.org/index.php?topic=1443633.7200)

## Coin Specs

Coin specifications ||
:------------ | :---:
Algorithm | hybrid: X11 (PoW) / SHA-256 (PoS) |
Retargeting Algorithm | DGW |
Difficulty Retargeting | Every Block
Max Coin Supply | Unlimited
Premine | 16,400,000 ION*

_*16,629,951 Ion Premine was burned in block [1](https://chainz.cryptoid.info/ion/block.dws?000000ed2f68cd6c7935831cc1d473da7c6decdb87e8b5dba0afff0b00002690.htm)_

### Reward Distribution

#### Genesis block

| Block Height | Reward Amount | Notes |
 ------------: | ------------: | :---: |
1 | 16,400,000 ION | Initial Pre-mine |

### PoW Rewards Breakdown

Block Height | Masternodes | Miner | Total | Max coin supply |
 ----------: | ----------: | ----: | ----: | ----: |
2-454 | 50% (11.5 ION) | 50% (11.5 ION) | 10,419 ION | 16,410,419 ION |

### PoS/PoW Rewards Breakdown

Block Height | Masternodes | Miner/Minter | Budget | Max coin supply |
 ----------: | ----------: | ----: | -----: | ----: |
455-1000 | 50% (11.5 ION) | 50% (11.5 ION)| 12,558 ION | 16,422,977 ION |

### PoS Rewards Breakdown

Block Height | Masternodes | Minter | Budget | Max coin supply |
 ----------: | ----------: | ----: | -----: | ----: |
1001-125146 | 50% (11.5 ION) | 50% (11.5 ION) | 2,855,358 ION | 19,278,335 ION |
125147-550001 | 50% (8.5 ION) | 50% (8.5 ION) | 7,222,535 ION | 26,500,870 ION |
550002-551441 | 50% (0.01 ION) | 50% (0.01 ION) | 28.8 ION | 26,500,898.8 ION |
551442-570063 | 50% (8.5 ION) | 50% (8.5 ION) | 316,574 ION | 26,817,472.8 ION |
570064-1013539 | 50% (5.75 ION) | 50% (5.75 ION) | 5,099,974 ION | 31,917,446.8 ION |
1013540-1600000 | 50% (2.875 ION) | 50% (2.875 ION) | 3,372,150.75 ION | 35,289,597.55 ION |

### Hybrid PoS/PoW Rewards Breakdown

Block Height | Masternodes | Miner | Minter | Annual (ION) | Annual (ELEC) |
 ----------: | ----------: | ----: | -----: | ----: | ----: |
1600000 - | 70% ION (0.35 ION) | 100% ELEC (0.5 ELEC)| 30% ION (0.15 ION)| 262,800 ION | 262,800 ELEC |

## Ion Core FAQ/Help
_get support/chat with us or send your request per [Email](mail:support@ionomy.com). You can also join our discord or encrypted chat on matrix._
[![Twitter](https://img.shields.io/twitter/url/https/bitbucket.org/ioncoin/ion.svg?style=social)](https://twitter.com/intent/tweet?text=Wow:&url=https%3A%2F%2Fgithub.com%2Fioncoincore%2Fion)

- [Documentation](../../tree/master/doc)
  - [Setup Ion](../../tree/master/doc#setup)
  - [Running Ion Core](../../tree/master/doc#running)
    - [Windows](https://bitbucket.org/ioncoin/ion/tree/master/doc#windows)
    - [Unix](https://bitbucket.org/ioncoin/ion/tree/master/doc#unix)
    - [OSX](https://bitbucket.org/ioncoin/ion/tree/master/doc#osx)
- [Ion Core CE Wiki](../../wiki)
  - [Accounts-Explained](../../wiki/Accounts-Explained)
  - [API Call list](../../wiki/API-Calls-List)
  - [API-Reference-JSON-RPC](../../wiki/API-Reference-JSON-RPC)
  - [Data-Directory](../../wiki/Data-Directory)
- External resources
  - [Ionomy.com](https://ionomy.com)
    - [Telegram](https://t.me/ionomy)
    - [Twitter](https://twitter.com/ionomics)
    - [ionomy github](https://github.com/ionomy)
    - [reddit](https://www.reddit.com/r/ionomy/)
    - [facebook](https://facebook.com/ionomy)

## Documentation

- [Client Documentation](../../tree/master/doc)
- [Technical documentation (doxygen)](techdocu.ioncore.xyz)
