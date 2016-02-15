## Compiling

1. navigate to the source folder:

        $ cd ./source

2. make a clean compile of some or all modules:

        $ make clean
        $ make epoll_clnt
        $ make epoll_svr
        $ make select_svr
        $ make thread_svr

## Running a server

1. epoll server

        $ ./epoll_svr.out -p [listening port] -n [number of processes]

2. select server

        $ ./select_svr.out -p [listening port] -n [number of processes]

3. threaded server

        $ ./thread_svr.out -p [listening port] -n [number of pre-spawned threads]

## Running the client

    $ ./epoll_clnt.out -h [server address] -p [server port] -n [number of processes] -c [number of clients] -r [echo requests per connection] -d [echoed text] -t [timeout]
