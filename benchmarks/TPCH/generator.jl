using tpch_dbgen_jll: tpch_dbgen_jll
using RelationalAIBase: _cmd

function generate_validation_query(config, i::Int)
    qgen = _cmd(config.skewed ? `$(tpch_dbgen_jll.qgen_jcch()) -k` : tpch_dbgen_jll.qgen_rel())
    config.skewed && @warn "Using skewed paramater generator"
    return read(addenv(setenv(`$qgen -d -s $(config.scale_factor) $i`; dir=config.tpch_dir), Dict("DSS_QUERY" => config.query_dir)), String)
end

function generate_query(config, i::Int)
    qgen = _cmd(config.skewed ? `$(tpch_dbgen_jll.qgen_jcch()) -k` : tpch_dbgen_jll.qgen_rel())
    return read(addenv(setenv(`$qgen -r $(config.qgen_seed) -s $(config.scale_factor) $i`; dir=config.tpch_dir), Dict("DSS_PATH" => "/tmp", "DSS_QUERY" => config.query_dir)), String)
end

function generate_data(config)
    dbgen = _cmd(config.skewed ? `$(tpch_dbgen_jll.dbgen_jcch()) -k` : tpch_dbgen_jll.dbgen_rel())
    sf = config.scale_factor
    output = abspath(config.data_dir)
    info("generating dataset in $output")
    mkpath(output, mode=0o700)
    mkpath(joinpath(output, "refresh"), mode=0o700)

    info("generating dataset for SF$sf")
    cd(config.tpch_dir) do
        run(addenv(`$dbgen -q -f -s $sf`, Dict("DSS_PATH" => output)))
    end

    x = get_streams(sf)+config.runs
    info("generating $x update sets for SF$sf")
    cd(config.tpch_dir) do
        run(addenv(`$dbgen -q -U $x -f -s $sf`, Dict("DSS_PATH" => joinpath(output, "refresh"))))
    end

    if config.compressed
        cd(output) do
            run(`gzip -r .`)
        end
    end
end
