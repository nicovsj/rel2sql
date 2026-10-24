#!/usr/bin/env julia
# One-shot Rel runner (subprocess fallback). For batch work use run_rel_engine_server.jl.
# Usage: julia run_rel_program.jl <request.json>

include(joinpath(@__DIR__, "rel_engine_core.jl"))
using .RelEngineCore
using JSON

function main()
    try
        if length(ARGS) < 1
            println(stderr, "Usage: julia run_rel_program.jl <request.json>")
            exit(2)
        end

        request = JSON.parse(read(ARGS[1], String))
        response = RelEngineCore.run_request(request)
        if haskey(response, "error")
            println(JSON.json(response))
            exit(1)
        end
        println(JSON.json(response))
    catch exc
        msg = sprint(showerror, exc)
        println(JSON.json(Dict("error" => msg)))
        exit(1)
    end
end

main()
