cmd_fs/lockd/lockd.o := arm-linux-ld -EL    -r -o fs/lockd/lockd.o fs/lockd/clntlock.o fs/lockd/clntproc.o fs/lockd/host.o fs/lockd/svc.o fs/lockd/svclock.o fs/lockd/svcshare.o fs/lockd/svcproc.o fs/lockd/svcsubs.o fs/lockd/mon.o fs/lockd/xdr.o fs/lockd/grace.o fs/lockd/xdr4.o fs/lockd/svc4proc.o 
