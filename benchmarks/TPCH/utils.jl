using tpch_dbgen_jll: tpch_dbgen_jll

# TODO: use better logging facility when available:
function info(msg)
    datestring =  Dates.format(now(), "yyyy:mm:dd HH:MM:SS")
    println("$datestring [ tpch | info ] $msg")
end

function tpch_dir(dbgen = "dbgen")
    local_tpch_dbgen = abspath(joinpath(tpch_dbgen_jll.artifact_dir, "tpch-dbgen"))
    if haskey(Base.ENV, "TPCH_DIR") && ispath(joinpath(Base.ENV["TPCH_DIR"], dbgen))
        Base.ENV["TPCH_DIR"]
    elseif ispath(joinpath(local_tpch_dbgen, dbgen))
        local_tpch_dbgen
    else
        try
            dirname(strip(read(`bash -c "type -P $dbgen"`, String)))
        catch
            error("$dbgen not found. Please point TPCH_DIR environment variable to the directory that contains $dbgen, or make sure $dbgen is available in PATH.")
        end
    end
end

function create_tpch_database(con, scale_factor)
    conf = rel_config(scale_factor, con, skewed=false)
    ispath(joinpath(TOP_SRC_DIR, "src/TPCH/data/sf-$scale_factor")) || generate_data(conf)
    init_db(conf)
end
