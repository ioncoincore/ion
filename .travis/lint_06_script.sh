#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C

contrib/devtools/git-subtree-check.sh src/leveldb
contrib/devtools/git-subtree-check.sh src/secp256k1
contrib/devtools/git-subtree-check.sh src/crypto/ctaes
contrib/devtools/git-subtree-check.sh src/univalue
#contrib/devtools/git-subtree-check.sh snap

# Fails with error 10 as not all commands are documented
#contrib/devtools/check-doc.py

#if [ "$TRAVIS_EVENT_TYPE" = "pull_request" ]; then
  contrib/devtools/lint-whitespace.sh
#fi
