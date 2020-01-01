#!/usr/bin/env python3
# Copyright (c) 2018-2019 The Bitcoin Core developers
# Copyright (c) 2018-2020 The Ion Core Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# how to use and guide: https://github.com/ioncoincore/docs/blob/master/gitian-building.md
# Setup: gitian-build.py --setup --build $SIGNER $VERSION 192.168.1.37 SHA256 ion-binaries
#usage: gitian-build.py [-h] [-c] [-p] [-u URL] [-v] [-b] [-s] [-B] [-o OS]
#                       [-j JOBS] [-m MEMORY] [-k] [-d] [-S] [-D] [-n] [-z]
#                       [-x SERVER] [-l] [-f UPLOADFOLDER] [-y HASH]
#                       [signer] [version]
import argparse
import os
import subprocess
import sys

def setup():
    global args, workdir
    programs = ['ruby', 'git', 'make', 'wget', 'curl']
    if args.kvm:
        programs += ['apt-cacher-ng', 'python-vm-builder', 'qemu-kvm', 'qemu-utils']
    elif args.docker and not os.path.isfile('/lib/systemd/system/docker.service'):
        dockers = ['docker.io', 'docker-ce']
        for i in dockers:
            return_code = subprocess.call(['sudo', 'apt-get', 'install', '-qq', i])
            if return_code == 0:
                break
        if return_code != 0:
            print('Cannot find any way to install Docker.', file=sys.stderr)
            sys.exit(1)
    else:
        programs += ['apt-cacher-ng', 'lxc', 'debootstrap']
    subprocess.check_call(['sudo', 'apt-get', 'install', '-qq'] + programs)
    if not os.path.isdir('gitian.sigs'):
        subprocess.check_call(['git', 'clone', 'https://bitbucket.org/ioncoin/gitian.sigs.git', 'gitian.sigs'])
    if not os.path.isdir('ion-detached-sigs'):
        subprocess.check_call(['git', 'clone', 'https://bitbucket.org/ioncoin/ion-detached-sigs.git'])
    if not os.path.isdir('gitian-builder'):
        subprocess.check_call(['git', 'clone', 'https://github.com/devrandom/gitian-builder.git'])
    if not os.path.isdir('ion'):
        subprocess.check_call(['git', 'clone', '--recurse-submodules', 'https://bitbucket.org/ioncoin/ion.git'])
    os.chdir('gitian-builder')
    make_image_prog = ['bin/make-base-vm', '--suite', 'bionic', '--arch', 'amd64']
    if args.docker:
        make_image_prog += ['--docker']
    elif not args.kvm:
        make_image_prog += ['--lxc']
    subprocess.check_call(make_image_prog)
    os.chdir(workdir)
    if args.is_bionic and not args.kvm and not args.docker:
        subprocess.check_call(['sudo', 'sed', '-i', 's/lxcbr0/br0/', '/etc/default/lxc-net'])
        print('Reboot is required')
        sys.exit(0)

def build():
    global args, workdir

    os.makedirs('ion-binaries/' + args.version, exist_ok=True)
    print('\nBuilding Dependencies\n')
    os.chdir('gitian-builder')
    os.makedirs('inputs', exist_ok=True)

    subprocess.check_call(['wget', '-O', 'inputs/osslsigncode-2.0.tar.gz', 'https://github.com/mtrojnar/osslsigncode/archive/2.0.tar.gz'])
    subprocess.check_call(["echo '5a60e0a4b3e0b4d655317b2f12a810211c50242138322b16e7e01c6fbb89d92f inputs/osslsigncode-2.0.tar.gz' | sha256sum -c"], shell=True)
    subprocess.check_call(['make', '-C', '../dash/depends', 'download', 'SOURCES_PATH=' + os.getcwd() + '/cache/common'])

    if args.linux:
        print('\nCompiling ' + args.version + ' Linux')
        subprocess.check_call(['bin/gbuild', '-j', args.jobs, '-m', args.memory, '--commit', 'ion='+args.commit, '--url', 'ion='+args.url, '../ion/contrib/gitian-descriptors/gitian-linux.yml'])
        subprocess.check_call(['bin/gsign', '-p', args.sign_prog, '--signer', args.signer, '--release', args.version+'-linux', '--destination', '../gitian.sigs/', '../ion/contrib/gitian-descriptors/gitian-linux.yml'])
        subprocess.check_call('mv build/out/ioncore-*.tar.gz build/out/src/ioncore-*.tar.gz ../ion-binaries/'+args.version, shell=True)

    if args.windows:
        print('\nCompiling ' + args.version + ' Windows')
        subprocess.check_call(['bin/gbuild', '-j', args.jobs, '-m', args.memory, '--commit', 'ion='+args.commit, '--url', 'ion='+args.url, '../ion/contrib/gitian-descriptors/gitian-win.yml'])
        subprocess.check_call(['bin/gsign', '-p', args.sign_prog, '--signer', args.signer, '--release', args.version+'-win-unsigned', '--destination', '../gitian.sigs/', '../ion/contrib/gitian-descriptors/gitian-win.yml'])
        subprocess.check_call('mv build/out/ioncore-*-win-unsigned.tar.gz inputs/', shell=True)
        subprocess.check_call('mv build/out/ioncore-*.zip build/out/ioncore-*.exe ../ion-binaries/'+args.version, shell=True)

    if args.macos:
        print('\nCompiling ' + args.version + ' MacOS')
        subprocess.check_call(['bin/gbuild', '-j', args.jobs, '-m', args.memory, '--commit', 'ion='+args.commit, '--url', 'ion='+args.url, '../ion/contrib/gitian-descriptors/gitian-osx.yml'])
        subprocess.check_call(['bin/gsign', '-p', args.sign_prog, '--signer', args.signer, '--release', args.version+'-osx-unsigned', '--destination', '../gitian.sigs/', '../ion/contrib/gitian-descriptors/gitian-osx.yml'])
        subprocess.check_call('mv build/out/ioncore-*-osx-unsigned.tar.gz inputs/', shell=True)
        subprocess.check_call('mv build/out/ioncore-*.tar.gz build/out/ioncore-*.dmg ../ion-binaries/'+args.version, shell=True)

    os.chdir(workdir)

    if args.hash:
        os.chdir('ion-binaries/'+args.version)
        subprocess.check_call('sha'+args.hash+'sum ion* > SHA'+args.hash+'SUMS', shell=True)
        subprocess.check_call('gpg -u '+args.signer+' --digest-algo sha'+args.hash+' --clearsign SHA'+args.hash+'SUMS', shell=True)

    os.chdir(workdir)

    if args.commit_files:
        print('\nCommitting '+args.version+' Unsigned Sigs\n')
        os.chdir('gitian.sigs')
        subprocess.check_call(['git', 'add', args.version+'-linux/'+args.signer])
        subprocess.check_call(['git', 'add', args.version+'-win-unsigned/'+args.signer])
        subprocess.check_call(['git', 'add', args.version+'-osx-unsigned/'+args.signer])
        subprocess.check_call(['git', 'commit', '-m', 'Add '+args.version+' unsigned sigs for '+args.signer])
        os.chdir(workdir)

    os.chdir(workdir)

def sign():
    global args, workdir
    os.chdir('gitian-builder')

    if args.windows:
        print('\nSigning ' + args.version + ' Windows')
        subprocess.check_call('cp inputs/ioncore-' + args.version + '-win-unsigned.tar.gz inputs/ioncore-win-unsigned.tar.gz', shell=True)
        subprocess.check_call(['bin/gbuild', '-i', '--commit', 'signature='+args.commit, '../ion/contrib/gitian-descriptors/gitian-win-signer.yml'])
        subprocess.check_call(['bin/gsign', '-p', args.sign_prog, '--signer', args.signer, '--release', args.version+'-win-signed', '--destination', '../gitian.sigs/', '../ion/contrib/gitian-descriptors/gitian-win-signer.yml'])
        subprocess.check_call('mv build/out/ioncore-*win64-setup.exe ../ion-binaries/'+args.version, shell=True)
        subprocess.check_call('mv build/out/ioncore-*win32-setup.exe ../ion-binaries/'+args.version, shell=True)

    if args.macos:
        print('\nSigning ' + args.version + ' MacOS')
        subprocess.check_call('cp inputs/ioncore-' + args.version + '-osx-unsigned.tar.gz inputs/ioncore-osx-unsigned.tar.gz', shell=True)
        subprocess.check_call(['bin/gbuild', '-i', '--commit', 'signature='+args.commit, '../ion/contrib/gitian-descriptors/gitian-osx-signer.yml'])
        subprocess.check_call(['bin/gsign', '-p', args.sign_prog, '--signer', args.signer, '--release', args.version+'-osx-signed', '--destination', '../gitian.sigs/', '../ion/contrib/gitian-descriptors/gitian-osx-signer.yml'])
        subprocess.check_call('mv build/out/ioncore-osx-signed.dmg ../ion-binaries/'+args.version+'/ioncore-'+args.version+'-osx.dmg', shell=True)

    os.chdir(workdir)

    if args.commit_files:
        print('\nCommitting '+args.version+' Signed Sigs\n')
        os.chdir('gitian.sigs')
        subprocess.check_call(['git', 'add', args.version+'-win-signed/'+args.signer])
        subprocess.check_call(['git', 'add', args.version+'-osx-signed/'+args.signer])
        subprocess.check_call(['git', 'commit', '-a', '-m', 'Add '+args.version+' signed binary sigs for '+args.signer])
        os.chdir(workdir)

def createhashes():
    global args, workdir
    os.chdir(workdir)
    os.chdir('ion-binaries/'+args.version)
    subprocess.check_call('sha'+args.hash+'sum ion* > SHA'+args.hash+'SUMS', shell=True)
    subprocess.check_call('gpg -u '+args.signer+' --digest-algo sha'+args.hash+' --clearsign SHA'+args.hash+'SUMS', shell=True)
    subprocess.check_call('rm', '-f', '/SHA'+args.hash+'SUMS', shell=True)
    os.chdir(workdir)

def sshupload():
    global args, workdir
    os.chdir(workdir)
    print('\n'+args.server+': Start uploading all files to the uploadserver.\n')
    subprocess.check_call(['ssh', args.server, 'mkdir', '-p', args.uploadfolder+'/'+args.version])
    subprocess.check_call(['scp', '-r', args.uploadfolder+'/'+args.version, args.server+':'+args.uploadfolder+'/'+args.version])
    os.chdir(workdir)

def logsupload():
    global args, workdir
    os.chdir(workdir)

    if args.uploadlogs:
        print('\n'+args.server+': Start uploading logs to the uploadserver.\n')
        subprocess.check_call(['ssh', args.server, 'mkdir', '-p', args.uploadfolder+'/'+args.version+'/logs'])
        subprocess.check_call(['ssh', args.server, 'mkdir', '-p', args.uploadfolder+'/'+args.version+'/result'])
        subprocess.check_call(['scp', '-r', 'gitian-builder/var', args.server+':'+args.uploadfolder+'/'+args.version+'/logs'])
        subprocess.check_call(['scp', 'gitian-builder/result/ioncore-*.yml', args.server+':'+args.uploadfolder+'/'+args.version+'/result'])

    if args.createreleasenotes:
        print('\n'+args.server+': Start uploading release notes to the uploadserver.\n')
        subprocess.check_call(['ssh', args.server, 'mkdir', '-p', args.uploadfolder+'/'+args.version+'/release-notes'])
        subprocess.check_call(['scp', '-r', args.uploadfolder+'/'+args.version+'/release-notes', args.server+':'+args.uploadfolder+'/'+args.version+'/release-notes'])

    os.chdir(workdir)

def releasenotes():
    global args, workdir
    os.chdir(workdir)

    if not os.path.isdir(args.uploadfolder+'/'+args.version+'/release-notes'):
        subprocess.check_call('mkdir -p '+args.uploadfolder+'/'+args.version+'/release-notes', shell=True)

    os.chdir('ion')
    
    if args.previousver:
        # Create shortlog notes
        subprocess.check_call('git shortlog --no-merges v'+args.previousver+'..v'+args.version+' > ../ion-binaries/'+args.version+'/release-notes/shortlog_v'+args.previousver+'..v'+args.version+'.md', shell=True)
        # Create changes notes
        subprocess.check_call('git log --oneline v'+args.previousver+'..v'+args.version+' > ../ion-binaries/'+args.version+'/release-notes/changes_'+args.previousver+'-v'+args.version+'.md', shell=True)
        # Create authors notes
        subprocess.check_call('git log --format='"'- %aN' v'+args.previousver+'..v'+args.version+' | sort -fiu > ../ion-binaries/"+args.version+'/release-notes/authors_'+args.previousver+'-v'+args.version+'.md', shell=True)
        # Create detailed notes
        subprocess.check_call('git log v'+args.previousver+'..v'+args.version+' --pretty="format:%at %C(yellow)commit %H%Creset\nAuthor: %an <%ae>\nDate: %aD\n\n %s\n" | sort -r | cut -d" " -f2- | sed -e "s/\\\n/\\`echo -e '+"'"+"\n\r'`/g"+'" | tr -d '+"'\15\32'"+' | less -R  > ../ion-binaries/'+args.version+'/release-notes/detailedlog_'+args.previousver+'-v'+args.version+'.md', shell=True)

    os.chdir(workdir)

def verify():
    global args, workdir
    rc = 0
    os.chdir('gitian-builder')

    print('\nVerifying v'+args.version+' Linux\n')
    if subprocess.call(['bin/gverify', '-v', '-d', '../gitian.sigs/', '-r', args.version+'-linux', '../ion/contrib/gitian-descriptors/gitian-linux.yml']):
        print('Verifying v'+args.version+' Linux FAILED\n')
        rc = 1

    print('\nVerifying v'+args.version+' Windows\n')
    if subprocess.call(['bin/gverify', '-v', '-d', '../gitian.sigs/', '-r', args.version+'-win-unsigned', '../ion/contrib/gitian-descriptors/gitian-win.yml']):
        print('Verifying v'+args.version+' Windows FAILED\n')
        rc = 1

    print('\nVerifying v'+args.version+' MacOS\n')
    if subprocess.call(['bin/gverify', '-v', '-d', '../gitian.sigs/', '-r', args.version+'-osx-unsigned', '../ion/contrib/gitian-descriptors/gitian-osx.yml']):
        print('Verifying v'+args.version+' MacOS FAILED\n')
        rc = 1

    print('\nVerifying v'+args.version+' Signed Windows\n')
    if subprocess.call(['bin/gverify', '-v', '-d', '../gitian.sigs/', '-r', args.version+'-win-signed', '../ion/contrib/gitian-descriptors/gitian-win-signer.yml']):
        print('Verifying v'+args.version+' Signed Windows FAILED\n')
        rc = 1

    print('\nVerifying v'+args.version+' Signed MacOS\n')
    if subprocess.call(['bin/gverify', '-v', '-d', '../gitian.sigs/', '-r', args.version+'-osx-signed', '../ion/contrib/gitian-descriptors/gitian-osx-signer.yml']):
        print('Verifying v'+args.version+' Signed MacOS FAILED\n')
        rc = 1

    os.chdir(workdir)
    return rc

def main():
    global args, workdir

    parser = argparse.ArgumentParser(description='Script for running full Gitian builds.')
    parser.add_argument('-c', '--commit', action='store_true', dest='commit', help='Indicate that the version argument is for a commit or branch')
    parser.add_argument('-p', '--pull', action='store_true', dest='pull', help='Indicate that the version argument is the number of a github repository pull request')
    parser.add_argument('-u', '--url', dest='url', default='https://bitbucket.org/ioncoin/ion', help='Specify the URL of the repository. Default is %(default)s')
    parser.add_argument('-v', '--verify', action='store_true', dest='verify', help='Verify the Gitian build')
    parser.add_argument('-b', '--build', action='store_true', dest='build', help='Do a Gitian build')
    parser.add_argument('-s', '--sign', action='store_true', dest='sign', help='Make signed binaries for Windows and MacOS')
    parser.add_argument('-B', '--buildsign', action='store_true', dest='buildsign', help='Build both signed and unsigned binaries')
    parser.add_argument('-o', '--os', dest='os', default='lwm', help='Specify which Operating Systems the build is for. Default is %(default)s. l for Linux, w for Windows, m for MacOS')
    parser.add_argument('-j', '--jobs', dest='jobs', default='2', help='Number of processes to use. Default %(default)s')
    parser.add_argument('-m', '--memory', dest='memory', default='2000', help='Memory to allocate in MiB. Default %(default)s')
    parser.add_argument('-k', '--kvm', action='store_true', dest='kvm', help='Use KVM instead of LXC')
    parser.add_argument('-d', '--docker', action='store_true', dest='docker', help='Use Docker instead of LXC')
    parser.add_argument('-S', '--setup', action='store_true', dest='setup', help='Set up the Gitian building environment. Only works on Debian-based systems (Ubuntu, Debian)')
    parser.add_argument('-D', '--detach-sign', action='store_true', dest='detach_sign', help='Create the assert file for detached signing. Will not commit anything.')
    parser.add_argument('-n', '--no-commit', action='store_false', dest='commit_files', help='Do not commit anything to git')
    parser.add_argument('-z', '--no-upload',  action='store_false', dest='upload', help='If upload is enabled, files will be uploaded to defined server. If not specified, upload is enabled by default')
    parser.add_argument('-x', '--server', dest='server', default='defaultuploadserver', help='Use scp to upload file to the server, defines in .ssh as uploadserver, pass serverIp and path to ssh private key. Default is which is configured in your ~/.ssh/config file')
    parser.add_argument('-l', '--uploadlogs', action='store_true', dest='uploadlogs', help='Upload logs and scripts (var folder)')
    parser.add_argument('-f', '--uploadfolder', dest='uploadfolder', default='ion-binaries', help='Upload folder on uploadserver')
    parser.add_argument('-y', '--hash', dest='hash', default='256', help='Create SHA hashes, choose beetwen SHA1, SHA256, SHA512')
    parser.add_argument('-r', '--createreleasenotes', action='store_true', dest='createreleasenotes', help='Create release notes and changes to previous version, previous version variable must be set')
    parser.add_argument('-P', '--previousver', dest='previousver', default='unset', help='Previous version for release notes, authors etc.')
    parser.add_argument('signer', nargs='?', help='GPG signer to sign each build assert file')
    parser.add_argument('version', nargs='?', help='Version number, commit, or branch to build. If building a commit or branch, the -c option must be specified')
    args = parser.parse_args()
    workdir = os.getcwd()

    args.is_bionic = b'bionic' in subprocess.check_output(['lsb_release', '-cs'])

    if args.kvm and args.docker:
        raise Exception('Error: cannot have both kvm and docker')

    # Ensure no more than one environment variable for gitian-builder (USE_LXC, USE_VBOX, USE_DOCKER) is set as they
    # can interfere (e.g., USE_LXC being set shadows USE_DOCKER; for details see gitian-builder/libexec/make-clean-vm).
    os.environ['USE_LXC'] = ''
    os.environ['USE_VBOX'] = ''
    os.environ['USE_DOCKER'] = ''
    if args.docker:
        os.environ['USE_DOCKER'] = '1'
    elif not args.kvm:
        os.environ['USE_LXC'] = '1'
        if 'GITIAN_HOST_IP' not in os.environ.keys():
            os.environ['GITIAN_HOST_IP'] = '10.0.3.1'
        if 'LXC_GUEST_IP' not in os.environ.keys():
            os.environ['LXC_GUEST_IP'] = '10.0.3.5'

    # Script will fail to automaticly download all resources if inputs folder does not exist
    subprocess.check_call(['mkdir', '-p', 'gitian-builder/inputs'])

    if args.setup:
        setup()

    if args.buildsign:
        args.build = True
        args.sign = True

    if not args.build and not args.sign and not args.verify:
        sys.exit(0)

    args.linux = 'l' in args.os
    args.windows = 'w' in args.os
    args.macos = 'm' in args.os

    # Disable for MacOS if no SDK found
    if args.macos and not os.path.isfile('gitian-builder/inputs/MacOSX10.11.sdk.tar.xz'):
        print('Cannot build for MacOS, SDK does not exist. Will build for other OSes')
        args.macos = False

    args.sign_prog = 'true' if args.detach_sign else 'gpg --detach-sign'

    script_name = os.path.basename(sys.argv[0])
    if not args.signer:
        print(script_name+': Missing signer')
        print('Try '+script_name+' --help for more information')
        sys.exit(1)
    if not args.version:
        print(script_name+': Missing version')
        print('Try '+script_name+' --help for more information')
        sys.exit(1)
    # Add leading 'v' for tags
    if args.commit and args.pull:
        raise Exception('Cannot have both commit and pull')
    args.commit = ('' if args.commit else 'v') + args.version

    os.chdir('ion')
    if args.pull:
        subprocess.check_call(['git', 'fetch', args.url, 'refs/pull/'+args.version+'/merge'])
        subprocess.check_call(['git', 'submodule', 'update', '--init', '--recursive'])
        os.chdir('../gitian-builder/inputs/ion')
        subprocess.check_call(['git', 'fetch', args.url, 'refs/pull/'+args.version+'/merge'])
        args.commit = subprocess.check_output(['git', 'show', '-s', '--format=%H', 'FETCH_HEAD'], universal_newlines=True, encoding='utf8').strip()
        args.version = 'pull-' + args.version
        subprocess.check_call(['git', 'submodule', 'update', '--init', '--recursive'])
    print(args.commit)
    subprocess.check_call(['git', 'fetch'])
    subprocess.check_call(['git', 'checkout', args.commit])
    subprocess.check_call(['git', 'submodule', 'update', '--init', '--recursive'])
    os.chdir(workdir)

    os.chdir('gitian-builder')
    subprocess.check_call(['git', 'pull'])
    os.chdir(workdir)

    if args.build:
        build()

    if args.sign:
        sign()

    if args.verify:
        os.chdir('gitian.sigs')
        subprocess.check_call(['git', 'pull'])
        os.chdir(workdir)
        sys.exit(verify())

    if args.hash:
        createhashes()

    if args.createreleasenotes:
        releasenotes()

    if args.uploadlogs:
        createhashes()

    if args.upload:
        sshupload()

if __name__ == '__main__':
    main()
