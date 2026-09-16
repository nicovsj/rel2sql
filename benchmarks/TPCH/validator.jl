module Validator

using Base: eachrow, sort!
using CSV: CSV, File
using DataFrames: DataFrame, dropmissing!, select!, Not
using Dates: Dates
using FixedPointDecimals: FixedPointDecimals

import Downloads

function _approx(l::Float64, r::Float64; atol::Real, rtol::Real)
    l == r || (isfinite(l) && isfinite(r) && abs(l-r) <= max(atol, rtol*max(abs(l), abs(r))))
end

_compare(x::Float64, y::Float64) = _approx(x, y; rtol=0, atol=1)
_compare(x::Any, y::Any) = x == y
_compare(x::Int64, y::T) where T <: Union{Dates.Date, Dates.DateTime} = x == Dates.value(y)
_compare(x::String, y::String) = strip(x) == strip(y)

"""
    validate(result, validation_file; order_matters=false)

Compare the values from `result` to `validation_file` and print the
difference to stdout. return `true` if the two files are almost identical.

RAI queries that use `top`, `bottom`, or `sort` should be validated with `order_matters=true`.
This removes the first index column in the RAI result and then does an ordere comparison.
"""
function validate(result::AbstractString, validation_file::AbstractString; order_matters::Bool=false)::Bool
    # use the files if they exist else compare the strings
    l = length(result) < 255 && isfile(result) ? open(result) : IOBuffer(result)

    r =  if startswith(validation_file, "https://")
        # Download the file
        Downloads.download(validation_file)
    else
        length(validation_file) < 255 && isfile(validation_file) ? validation_file : IOBuffer(validation_file)
    end

    df_result = File(l; header=false, delim='|', comment="#", stringtype=String) |> DataFrame
    df_validation_file = File(r; header=false, delim='|', comment="#", stringtype=String) |> DataFrame

    # Drop empty columns
    # TODO(CB) Remove this when we can export relations in wide format
    if !isempty(df_result)
        dropmissing!(df_result)
    end
    if !isempty(df_validation_file)
        dropmissing!(df_validation_file)
    end

    # If both files are empty return true
    isempty(df_result) && isempty(df_validation_file) && return true
    # Else, if one of the file is empty return false. (This will make sure that we're working on non-empty dataframes)
    if isempty(df_result) || isempty(df_validation_file)
        println("ValidationError: either the result of the query or the validation files are empty")
        return false
    end

    if order_matters
        # Remove the index column from the result:
        select!(df_result, Not(1))
    else # Order of results does not matter. We sort both for comparision:
        sort!(df_result)
        sort!(df_validation_file)
    end

    equal = true

    for rows in zip(eachrow(df_result), eachrow(df_validation_file))
        for column in zip(collect(first(rows)), collect(last(rows)))
            l = first(column)
            r = last(column)
            if !_compare(l, r)
                println("ValidationError: Found a difference in result $l != $r")
                equal = false
            end
        end
    end
    equal
end

end # module
