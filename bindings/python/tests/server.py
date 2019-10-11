#!/usr/bin/env python3

from server_upcalls import *
import os
import select
import subprocess

def main():
    try:
        foo = PMIxServer()
    except:
        print("FAILED TO CREATE SERVER")
        exit(1)
    print("Testing server version ", foo.get_version())
    args = [{'key':'FOOBAR', 'value':'VAR', 'val_type':PMIX_STRING},
            {'key':'BLAST', 'value':7, 'val_type':PMIX_INT32}]
    map = {'clientconnected': clientconnected,
           'clientfinalized': clientfinalized,
           'fencenb': clientfence,
           'publish': clientpublish,
           'unpublish': clientunpublish,
           'lookup': clientlookup,
           'query': clientquery,
           'registerevents': client_register_events}
    my_result = foo.init(args, map)
    print("Testing PMIx_Initialized")
    rc = foo.initialized()
    print("Initialized: ", rc)
    vers = foo.get_version()
    print("Version: ", vers)

    # pdata = {‘proc’: {‘nspace’: mynspace, ‘rank’: myrank}, ‘key’: ky,
    # ‘value’: v, ‘val_type’: t}
    pmix_locdat = []

    # get our environment as a base
    env = os.environ.copy()
    # register an nspace for the client app
    darray = {'type':PMIX_SIZE, 'array':[1, 2, 3, 4, 5]}
    kvals = [{'key':'testkey', 'value':darray, 'val_type':PMIX_DATA_ARRAY}]
    print("REGISTERING NSPACE")
    rc = foo.register_nspace("testnspace", 1, kvals)
    print("RegNspace ", rc)

    # register a client
    uid = os.getuid()
    gid = os.getgid()
    rc = foo.register_client({'nspace':"testnspace", 'rank':0}, uid, gid)
    print("RegClient ", rc)
    # setup the fork
    rc = foo.setup_fork({'nspace':"testnspace", 'rank':0}, env)
    print("SetupFrk", rc)

    # setup the client argv
    args = ["./client.py"]
    # open a subprocess with stdout and stderr
    # as distinct pipes so we can capture their
    # output as the process runs
    p = subprocess.Popen(args, env=env,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    # define storage to catch the output
    stdout = []
    stderr = []
    # loop until the pipes close
    while True:
        reads = [p.stdout.fileno(), p.stderr.fileno()]
        ret = select.select(reads, [], [])

        stdout_done = True
        stderr_done = True

        for fd in ret[0]:
            # if the data
            if fd == p.stdout.fileno():
                read = p.stdout.readline()
                if read:
                    read = read.decode('utf-8').rstrip()
                    stdout_done = False
                    stdout.append(read)
            elif fd == p.stderr.fileno():
                read = p.stderr.readline()
                if read:
                    read = read.decode('utf-8').rstrip()
                    stderr_done = False
                    stderr.append(read)

        if stdout_done and stderr_done:
            break
    for p in stdout:
        print("stdout:", p)
    for p in stderr:
        print("stderr:", p)
    print("FINALIZING")
    foo.finalize()


if __name__ == '__main__':
    global killer
    killer = GracefulKiller()
    main()
