module ExportResults

using DataFrames: DataFrames, DataFrame, rename!
using CSV: CSV
using Printf: Printf, @sprintf
using StatsBase: StatsBase, geomean, mean, median
using RAI_Common: format_as_bytes
using HydraMetrics: hydra_metric
using Dates: Dates
using ..TPCH: CAPTURED_METRICS

function to_array(d; by=x->x.duration)
    return map(by, sort!(d, by=x->x.query))

end

function write_output(config, f::String, s::String)
    outfile = "$(config.output_dir)/$f"
    if config.output_dir != ""
        mkpath("$(config.output_dir)/$(dirname(f))", mode=0o700)
        open(outfile, "w") do fp
            write(fp, s)
        end
        # @info "Wrote output to $outfile"
    end
    outfile
end

function add_stats(df)
    copy = append!(similar(df,0), df)
    function eachrow_apply(f, df)
        res = []
        for r in eachrow(df)
            push!(res, f(map(x -> r[x], names(df))))
        end
        res
    end

    df2 = DataFrame()
    df2.min = eachrow_apply(minimum, copy)
    df2.max = eachrow_apply(maximum, copy)
    df2.mean = eachrow_apply(mean, copy)
    df2.median = eachrow_apply(median, copy)

    df.min = df2.min
    df.max = df2.max
    df.mean = df2.mean
    df.median = df2.median
end


function print_tpch_result(config)
    result = config.result
    println()
    println("TPCH Report:")
    println()
    println("  Scale factor:   $(config.scale_factor)")
    println("  Load test:      $(result.metrics.load.total_time)")
    println("  Metadata size:  $(format_as_bytes(result.metrics.db_size.metadata_bytes))")
    println("  Relations size: $(format_as_bytes(result.metrics.db_size.relations_bytes))")
    println()

    hydra_metric("tpch.db_relations_size_bytes", @sprintf("%d", result.metrics.db_size.relations_bytes), "bytes")
    hydra_metric("tpch.db_metadata_size_bytes", @sprintf("%d", result.metrics.db_size.metadata_bytes), "bytes")

    df = DataFrame()

    if config.runs > 0
        for r = sort(collect(keys(result.metrics.performance_runs)))
            run = result.metrics.performance_runs[r]
            println("  Run $r:")
            println()
            println("    Power@Size:      $(run.power.power_size)")
            println("    Throughput@Size: $(run.throughput.throughput_size)")
            println("    QphH@Size:       $(run.power.qphh_size)")
            println()

            power_geomean = geomean([ qr.duration for qr in run.power.queries ])
            println("    Geometric mean Power:      $(power_geomean)")

            if !isempty(run.throughput.throughputs)
                tp_timings = []
                for s = keys(run.throughput.throughputs)
                    for q = keys(run.throughput.throughputs[s].queries)
                        push!(tp_timings, run.throughput.throughputs[s].queries[q].duration)
                    end
                end
                throughput_mean = mean(tp_timings)
                println("    Arithmetic mean Througput: $(throughput_mean)")
            end

            println()

            df[!, Symbol("P/$r")] = to_array(run.power.queries)
            for s = sort(collect(keys(run.throughput.throughputs)))
                df[!, Symbol("TP/$r/S$s")] = to_array(run.throughput.throughputs[s].queries)
            end
            println()
        end
        add_stats(df)

        println("  Queries:")
        println()
        show(df, allrows=true, allcols=true)

        for (i, q) in enumerate(config.supported_queries)
            hydra_metric("tpch.q$q.min", @sprintf("%.5f", df.min[i]))
            hydra_metric("tpch.q$q.max", @sprintf("%.5f", df.max[i]))
            hydra_metric("tpch.q$q.mean", @sprintf("%.5f", df.mean[i]))
            hydra_metric("tpch.q$q.median", @sprintf("%.5f", df.median[i]))

            hydra_metric("tpch.total.min", @sprintf("%.5f", sum(df.min)))
            hydra_metric("tpch.total.max", @sprintf("%.5f", sum(df.max)))
        end
    end

    if config.compiled_runs > 0
        println("\n\n")
        println("  Compiled Queries:")
        println()

        # We want to print results once for E2E time and once for QE time
        tx_metrics = [
            ("", (x) -> x.duration),
            ("qe_time.", (x) -> x.qe_duration),
        ]

        # Group runs by latency and force_dbopen (whether DB cache was cleared)
        grouped_runs = Dict()
        for run in result.metrics.compiled_runs
            key = (run.simulated_latency, run.force_dbopen, )
            if !haskey(grouped_runs, key)
                grouped_runs[key] = []
            end
            push!(grouped_runs[key], run)
        end
        for ((latency, force_dbopen, ),  runs) in grouped_runs
            println("\n\n  Latency: $latency ms\n\n")
            run_title = force_dbopen ? "dbopen" : "compiled"
            for (m_title, m_by_function) in tx_metrics
                df = DataFrame()
                for r in sort(collect(keys(runs)))
                    run = runs[r]
                    df[!, Symbol("$(run_title)_$(latency)ms_$(m_title)/$r")] = to_array(run.queries; by=m_by_function)
                end
                add_stats(df)

                hydra_prefix = "tpch.$(run_title).$(latency)ms"

                for (i, q) in enumerate(config.supported_queries)
                    hydra_metric("$(hydra_prefix).q$(q).$(m_title)median", @sprintf("%.5f", df.median[i]))
                end

                hydra_metric("$(hydra_prefix).$(m_title)total.min", @sprintf("%.5f", sum(df.min)))
                hydra_metric("$(hydra_prefix).$(m_title)total.max", @sprintf("%.5f", sum(df.max)))

                show(df, allrows=true, allcols=true)
            end
        end
    end

    println()
end

function make_queries_df(config)
    function add(db, sf::String, run, test, query::Int, stream, t, qe_t, simulated_latency, metrics)
        push!(coldata[1], "$db")
        push!(coldata[2], "$sf")
        push!(coldata[3], run)
        push!(coldata[4], test)
        push!(coldata[5], query)
        push!(coldata[6], "$stream")
        push!(coldata[7], t)
        push!(coldata[8], qe_t)
        push!(coldata[9], simulated_latency)
        for (i, metric) in enumerate(CAPTURED_METRICS)
            push!(coldata[9 + i], metrics[metric])
        end
    end

    col_types = [
        :db => String,
        :sf => String,
        :run => Int,
        :test => String,
        :query => Int,
        :stream => String,
        :time => Float64,
        :qe_time => Float64,
        :simulated_latency_ms => Float64,
        [Symbol(m) => Float64 for m in CAPTURED_METRICS]...,
    ]
    cols = map(first, col_types)
    coldata = map(t -> Vector{t}(undef, 0), map(last, col_types))

    runs = config.result.metrics.performance_runs
    for r in sort(collect(keys(runs)))
        for q in runs[r].power.queries
            add(
                config.display_name, config.scale_factor, r, "power", q.query, "",
                q.duration, q.qe_duration, 0.0, q.metrics
            )
        end

        for throughput in runs[r].throughput.throughputs
            for q in throughput.queries
                add(
                    config.display_name, config.scale_factor, r, "throughput", q.query,
                    throughput.streams, q.duration, q.qe_duration, 0.0, q.metrics
                )
            end
        end
    end

    let runs = config.result.metrics.compiled_runs
        for r in sort(collect(keys(runs)))
            run = runs[r]
            title = run.force_dbopen ? "dbopen" : "compiled"
            for q in run.queries
                add(
                    config.display_name, config.scale_factor, r, title, q.query, "",
                    q.duration, q.qe_duration, runs[r].simulated_latency, q.metrics
                )
            end
        end
    end

    df = DataFrame(coldata, :auto)
    rename!(df, cols)
    return df
end


function export_queries_csv(config)
    df = make_queries_df(config)

    # Do not overwrite, use timestamp for name
    path = joinpath(config.output_dir, "tpch-bench-results.csv")

    @info "Exporting final results to $path"

    CSV.write(path, df)
end

function timestring()
    Dates.format(Dates.now(), "YYYY_mm_dd:HH:MM:SS")
end

end #module
