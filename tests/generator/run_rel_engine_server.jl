#!/usr/bin/env julia
# Persistent Rel engine server over a Unix domain socket (NDJSON protocol).
# Usage: julia run_rel_engine_server.jl [socket_path]

include(joinpath(@__DIR__, "rel_engine_core.jl"))
using .RelEngineCore
using JSON
using Sockets

function default_socket_path()
    if haskey(ENV, "REL2SQL_REL_ENGINE_SOCKET") && !isempty(ENV["REL2SQL_REL_ENGINE_SOCKET"])
        return ENV["REL2SQL_REL_ENGINE_SOCKET"]
    end
    script_dir = @__DIR__
    root = script_dir
    for _ in 1:8
        if isfile(joinpath(root, "MODULE.bazel"))
            return joinpath(root, ".rel_engine.sock")
        end
        parent = dirname(root)
        if parent == root
            break
        end
        root = parent
    end
    return joinpath(tempdir(), "rel2sql_rel_engine.sock")
end

function write_response(sock, response)
    try
        println(sock, JSON.json(response))
        flush(sock)
    catch exc
        if exc isa Base.IOError || exc isa EOFError
            return
        end
        rethrow()
    end
end

function handle_connection(sock)
    try
        println(stderr, "rel_engine_server: client connected")
        flush(stderr)
        while isopen(sock)
            line = readline(sock)
            if line === nothing || isempty(strip(line))
                break
            end
            request = JSON.parse(String(line))
            op = get(request, "op", "run")
            if op != "ping"
                println(stderr, "rel_engine_server: received op=$op")
                flush(stderr)
            end
            response = RelEngineCore.handle_message(request)
            write_response(sock, response)
            flush(stderr)
            if op == "shutdown" || get(response, "shutdown", false)
                return :shutdown
            end
        end
    catch exc
        if exc isa Base.IOError || exc isa EOFError
            return :eof
        end
        write_response(sock, Dict("error" => sprint(showerror, exc)))
    end
    return :done
end

function write_pidfile!()
    pidfile = get(ENV, "REL2SQL_REL_ENGINE_PID", "")
    if isempty(pidfile)
        return
    end
    open(pidfile, "w") do io
        println(io, getpid())
    end
end

function remove_pidfile!()
    pidfile = get(ENV, "REL2SQL_REL_ENGINE_PID", "")
    if isempty(pidfile) || !isfile(pidfile)
        return
    end
    try
        current = parse(Int, strip(read(pidfile, String)))
        if current == getpid()
            rm(pidfile; force=true)
        end
    catch
        nothing
    end
end

function warmup_with_timer!(f, label::AbstractString)
    println(stderr, "rel_engine_server: $label")
    println(stderr, "  first start: typically 3-8 minutes; cached packages: ~30-60 seconds")
    start = time()
    done = Ref(false)
    timer = @async begin
        while !done[]
            elapsed = Int(round(time() - start))
            mins, secs = divrem(elapsed, 60)
            stamp = mins > 0 ? "$(mins)m $(lpad(secs, 2, '0'))s" : "$(secs)s"
            print(stderr, "\r  warming... $(stamp) elapsed")
            flush(stderr)
            sleep(1)
        end
    end
    try
        f()
    finally
        done[] = true
        try
            wait(timer)
        catch
            nothing
        end
        elapsed = Int(round(time() - start))
        println(stderr, "\r  warming... done in $(elapsed)s                    ")
        flush(stderr)
    end
end

function serve(socket_path::AbstractString)
    write_pidfile!()

    if ispath(socket_path)
        rm(socket_path; force=true)
    end

    warmup_with_timer!("warming RAICode...") do
        RelEngineCore.init_server!()
    end

    server = listen(socket_path)
    println(stderr, "")
    println(stderr, "rel_engine_server: ready")
    println(stderr, "  listening on: $(socket_path)")
    println(stderr, "  stop with: task rel-engine:stop  (or Ctrl+C in this terminal)")
    flush(stderr)

    try
        while true
            sock = accept(server)
            status = handle_connection(sock)
            close(sock)
            if status === :shutdown
                break
            end
        end
    finally
        close(server)
        RelEngineCore.shutdown_server!()
        if ispath(socket_path)
            rm(socket_path; force=true)
        end
        remove_pidfile!()
    end
end

function main()
    socket_path = length(ARGS) >= 1 ? ARGS[1] : default_socket_path()
    serve(socket_path)
end

main()
