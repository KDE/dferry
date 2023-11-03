static void socketpair(FileDescriptor fds[2])
{
    struct sockaddr_in inaddr;
    struct sockaddr addr;
    FileDescriptor listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    memset(&inaddr, 0, sizeof(inaddr));
    memset(&addr, 0, sizeof(addr));

    inaddr.sin_family = AF_INET;
    inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    inaddr.sin_port = 0;

    int trueConstant = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<char* >(&trueConstant), sizeof(trueConstant));
    bind(listener, reinterpret_cast<struct sockaddr *>(&inaddr), sizeof(inaddr));
    listen(listener, 1);

    int len = sizeof(inaddr);
    getsockname(listener, &addr, &len);
    fds[0] = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(fds[0], &addr, len);

    fds[1] = accept(listener, nullptr, nullptr);

    closesocket(listener);
}
