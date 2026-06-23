#!/usr/bin/env julia
# Run arbitrary Rel against RAICode (socket server or one-shot).
#
# Usage:
#   julia run_rel_snippet.jl program.rl
#   julia run_rel_snippet.jl --output myout program.rl
#   julia run_rel_snippet.jl --edb fixture.json program.rl
#   echo 'def output {1}' | julia run_rel_snippet.jl -
#   julia run_rel_snippet.jl --json program.rl
#   julia run_rel_snippet.jl --raw program.rl

include(joinpath(@__DIR__, "rel_engine_core.jl"))
using .RelEngineCore
using JSON
using Sockets

function usage()
    println(stderr, """
    Usage: run_rel_snippet.jl [options] <program.rl|->

    Options:
      --output NAME   Output relation name (default: output)
      --output-arity N  Column count when result is empty (default: 0 = infer from rows)
      --edb FILE      JSON object mapping table names to row arrays
      --socket PATH   Unix socket (default: REL2SQL_REL_ENGINE_SOCKET or .rel_engine.sock)
      --json          Print raw JSON response only
      --raw           Also print pre-normalized Rel values (types / structure)
      -h, --help      Show this help
    """)
end

function default_socket_path()
    if haskey(ENV, "REL2SQL_REL_ENGINE_SOCKET") && !isempty(ENV["REL2SQL_REL_ENGINE_SOCKET"])
        return ENV["REL2SQL_REL_ENGINE_SOCKET"]
    end
    root = @__DIR__
    for _ in 1:8
        if isfile(joinpath(root, "MODULE.bazel"))
            return joinpath(root, ".rel_engine.sock")
        end
        parent = dirname(root)
        parent == root && break
        root = parent
    end
    return joinpath(tempdir(), "rel2sql_rel_engine.sock")
end

function parse_args()
    output = "output"
    output_arity = 0
    edb = Dict{String, Any}()
    socket_path = default_socket_path()
    json_only = false
    show_raw = false
    program_path = nothing

    i = 1
    while i <= length(ARGS)
        arg = ARGS[i]
        if arg in ("-h", "--help")
            usage()
            exit(0)
        elseif arg == "--output"
            i += 1
            i > length(ARGS) && error("--output requires a value")
            output = ARGS[i]
        elseif arg == "--output-arity"
            i += 1
            i > length(ARGS) && error("--output-arity requires a value")
            output_arity = parse(Int, ARGS[i])
        elseif arg == "--edb"
            i += 1
            i > length(ARGS) && error("--edb requires a file path")
            edb = JSON.parse(read(ARGS[i], String))
        elseif arg == "--socket"
            i += 1
            i > length(ARGS) && error("--socket requires a path")
            socket_path = ARGS[i]
        elseif arg == "--json"
            json_only = true
        elseif arg == "--raw"
            show_raw = true
        elseif arg == "-"
            program_path = arg
            i == length(ARGS) || error("unexpected extra argument: $(ARGS[i+1])")
        elseif startswith(arg, "-")
            error("unknown option: $arg")
        else
            program_path = arg
            i == length(ARGS) || error("unexpected extra argument: $(ARGS[i+1])")
        end
        i += 1
    end

    program_path === nothing && error("missing program path (use - for stdin)")
    program = if program_path == "-"
        read(stdin, String)
    else
        read(program_path, String)
    end

    return (; program, output, output_arity, edb, socket_path, json_only, show_raw)
end

function run_via_socket(socket_path::AbstractString, request::AbstractDict)
    sock = connect(socket_path)
    try
        println(sock, JSON.json(request))
        flush(sock)
        line = readline(sock)
        line === nothing && error("empty response from rel engine server")
        return JSON.parse(String(line))
    finally
        close(sock)
    end
end

function print_raw(response::AbstractDict)
    raw = get(response, "raw", nothing)
    raw === nothing && return
    println("=== raw Rel result ===")
    shape = get(raw, "shape", "?")
    if shape == "vector"
        println("Vector ($(raw["length"]) values)")
        for (i, item) in enumerate(raw["items"])
            if get(item, "kind", "") == "tuple"
                println("  [$i] $(item["repr"])  (tuple, width=$(item["width"]))")
            else
                println("  [$i] $(item["repr"])  ($(item["kind"]))")
            end
        end
        truncated = get(raw, "truncated", 0)
        truncated > 0 && println("  ... ($truncated more)")
    else
        value = raw["value"]
        println("$(value["kind"]): $(value["repr"])")
    end
    println()
end

function print_table(response::AbstractDict)
    if haskey(response, "error")
        println("ERROR: ", response["error"])
        return
    end

    columns = get(response, "columns", String[])
    rows = get(response, "rows", Any[])

    println("columns: ", join(columns, ", "))
    println("rows ($(length(rows))):")
    for row in rows
        print("  ")
        println(join(row, " | "))
    end
end

function main()
    opts = parse_args()
    request = Dict(
        "op" => "run",
        "program" => opts.program,
        "output" => opts.output,
        "output_arity" => opts.output_arity,
        "edb" => opts.edb,
        "raw" => opts.show_raw,
    )

    response = if ispath(opts.socket_path)
        try
            run_via_socket(opts.socket_path, request)
        catch exc
            if exc isa Base.IOError || exc isa EOFError
                RelEngineCore.run_request(request)
            else
                rethrow()
            end
        end
    else
        RelEngineCore.run_request(request)
    end

    if opts.json_only
        println(JSON.json(response))
        return
    end

    if opts.show_raw
        print_raw(response)
    end

    println("=== Rel engine result ===")
    print_table(response)
    haskey(response, "error") && exit(1)
end

main()
