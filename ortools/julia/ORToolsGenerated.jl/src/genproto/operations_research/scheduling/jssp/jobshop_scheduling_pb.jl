# Autogenerated using ProtoBuf.jl v1.0.14 on 2024-01-02T18:44:47.157
# original file: /usr/local/google/home/tcuvelier/.julia/artifacts/cc3d5aa28fb2158ce4ff5aed9899545a37503a6b/include/ortools/scheduling/jssp/jobshop_scheduling.proto (proto3 syntax)

import ProtoBuf as PB
using ProtoBuf: OneOf
using ProtoBuf.EnumX: @enumx

export JobPrecedence, Task, AssignedTask, TransitionTimeMatrix, Job, AssignedJob, Machine
export JsspOutputSolution, JsspInputProblem

struct JobPrecedence
    first_job_index::Int32
    second_job_index::Int32
    min_delay::Int64
end
PB.default_values(::Type{JobPrecedence}) = (;first_job_index = zero(Int32), second_job_index = zero(Int32), min_delay = zero(Int64))
PB.field_numbers(::Type{JobPrecedence}) = (;first_job_index = 1, second_job_index = 2, min_delay = 3)

function PB.decode(d::PB.AbstractProtoDecoder, ::Type{<:JobPrecedence})
    first_job_index = zero(Int32)
    second_job_index = zero(Int32)
    min_delay = zero(Int64)
    while !PB.message_done(d)
        field_number, wire_type = PB.decode_tag(d)
        if field_number == 1
            first_job_index = PB.decode(d, Int32)
        elseif field_number == 2
            second_job_index = PB.decode(d, Int32)
        elseif field_number == 3
            min_delay = PB.decode(d, Int64)
        else
            PB.skip(d, wire_type)
        end
    end
    return JobPrecedence(first_job_index, second_job_index, min_delay)
end

function PB.encode(e::PB.AbstractProtoEncoder, x::JobPrecedence)
    initpos = position(e.io)
    x.first_job_index != zero(Int32) && PB.encode(e, 1, x.first_job_index)
    x.second_job_index != zero(Int32) && PB.encode(e, 2, x.second_job_index)
    x.min_delay != zero(Int64) && PB.encode(e, 3, x.min_delay)
    return position(e.io) - initpos
end
function PB._encoded_size(x::JobPrecedence)
    encoded_size = 0
    x.first_job_index != zero(Int32) && (encoded_size += PB._encoded_size(x.first_job_index, 1))
    x.second_job_index != zero(Int32) && (encoded_size += PB._encoded_size(x.second_job_index, 2))
    x.min_delay != zero(Int64) && (encoded_size += PB._encoded_size(x.min_delay, 3))
    return encoded_size
end

struct Task
    machine::Vector{Int32}
    duration::Vector{Int64}
    cost::Vector{Int64}
end
PB.default_values(::Type{Task}) = (;machine = Vector{Int32}(), duration = Vector{Int64}(), cost = Vector{Int64}())
PB.field_numbers(::Type{Task}) = (;machine = 1, duration = 2, cost = 3)

function PB.decode(d::PB.AbstractProtoDecoder, ::Type{<:Task})
    machine = PB.BufferedVector{Int32}()
    duration = PB.BufferedVector{Int64}()
    cost = PB.BufferedVector{Int64}()
    while !PB.message_done(d)
        field_number, wire_type = PB.decode_tag(d)
        if field_number == 1
            PB.decode!(d, wire_type, machine)
        elseif field_number == 2
            PB.decode!(d, wire_type, duration)
        elseif field_number == 3
            PB.decode!(d, wire_type, cost)
        else
            PB.skip(d, wire_type)
        end
    end
    return Task(machine[], duration[], cost[])
end

function PB.encode(e::PB.AbstractProtoEncoder, x::Task)
    initpos = position(e.io)
    !isempty(x.machine) && PB.encode(e, 1, x.machine)
    !isempty(x.duration) && PB.encode(e, 2, x.duration)
    !isempty(x.cost) && PB.encode(e, 3, x.cost)
    return position(e.io) - initpos
end
function PB._encoded_size(x::Task)
    encoded_size = 0
    !isempty(x.machine) && (encoded_size += PB._encoded_size(x.machine, 1))
    !isempty(x.duration) && (encoded_size += PB._encoded_size(x.duration, 2))
    !isempty(x.cost) && (encoded_size += PB._encoded_size(x.cost, 3))
    return encoded_size
end

struct AssignedTask
    alternative_index::Int32
    start_time::Int64
end
PB.default_values(::Type{AssignedTask}) = (;alternative_index = zero(Int32), start_time = zero(Int64))
PB.field_numbers(::Type{AssignedTask}) = (;alternative_index = 1, start_time = 2)

function PB.decode(d::PB.AbstractProtoDecoder, ::Type{<:AssignedTask})
    alternative_index = zero(Int32)
    start_time = zero(Int64)
    while !PB.message_done(d)
        field_number, wire_type = PB.decode_tag(d)
        if field_number == 1
            alternative_index = PB.decode(d, Int32)
        elseif field_number == 2
            start_time = PB.decode(d, Int64)
        else
            PB.skip(d, wire_type)
        end
    end
    return AssignedTask(alternative_index, start_time)
end

function PB.encode(e::PB.AbstractProtoEncoder, x::AssignedTask)
    initpos = position(e.io)
    x.alternative_index != zero(Int32) && PB.encode(e, 1, x.alternative_index)
    x.start_time != zero(Int64) && PB.encode(e, 2, x.start_time)
    return position(e.io) - initpos
end
function PB._encoded_size(x::AssignedTask)
    encoded_size = 0
    x.alternative_index != zero(Int32) && (encoded_size += PB._encoded_size(x.alternative_index, 1))
    x.start_time != zero(Int64) && (encoded_size += PB._encoded_size(x.start_time, 2))
    return encoded_size
end

struct TransitionTimeMatrix
    transition_time::Vector{Int64}
end
PB.default_values(::Type{TransitionTimeMatrix}) = (;transition_time = Vector{Int64}())
PB.field_numbers(::Type{TransitionTimeMatrix}) = (;transition_time = 1)

function PB.decode(d::PB.AbstractProtoDecoder, ::Type{<:TransitionTimeMatrix})
    transition_time = PB.BufferedVector{Int64}()
    while !PB.message_done(d)
        field_number, wire_type = PB.decode_tag(d)
        if field_number == 1
            PB.decode!(d, wire_type, transition_time)
        else
            PB.skip(d, wire_type)
        end
    end
    return TransitionTimeMatrix(transition_time[])
end

function PB.encode(e::PB.AbstractProtoEncoder, x::TransitionTimeMatrix)
    initpos = position(e.io)
    !isempty(x.transition_time) && PB.encode(e, 1, x.transition_time)
    return position(e.io) - initpos
end
function PB._encoded_size(x::TransitionTimeMatrix)
    encoded_size = 0
    !isempty(x.transition_time) && (encoded_size += PB._encoded_size(x.transition_time, 1))
    return encoded_size
end

struct Job
    tasks::Vector{Task}
    earliest_start::Union{Nothing,google.protobuf.Int64Value}
    early_due_date::Int64
    late_due_date::Int64
    earliness_cost_per_time_unit::Int64
    lateness_cost_per_time_unit::Int64
    latest_end::Union{Nothing,google.protobuf.Int64Value}
    name::String
end
PB.default_values(::Type{Job}) = (;tasks = Vector{Task}(), earliest_start = nothing, early_due_date = zero(Int64), late_due_date = zero(Int64), earliness_cost_per_time_unit = zero(Int64), lateness_cost_per_time_unit = zero(Int64), latest_end = nothing, name = "")
PB.field_numbers(::Type{Job}) = (;tasks = 1, earliest_start = 2, early_due_date = 3, late_due_date = 4, earliness_cost_per_time_unit = 5, lateness_cost_per_time_unit = 6, latest_end = 7, name = 16)

function PB.decode(d::PB.AbstractProtoDecoder, ::Type{<:Job})
    tasks = PB.BufferedVector{Task}()
    earliest_start = Ref{Union{Nothing,google.protobuf.Int64Value}}(nothing)
    early_due_date = zero(Int64)
    late_due_date = zero(Int64)
    earliness_cost_per_time_unit = zero(Int64)
    lateness_cost_per_time_unit = zero(Int64)
    latest_end = Ref{Union{Nothing,google.protobuf.Int64Value}}(nothing)
    name = ""
    while !PB.message_done(d)
        field_number, wire_type = PB.decode_tag(d)
        if field_number == 1
            PB.decode!(d, tasks)
        elseif field_number == 2
            PB.decode!(d, earliest_start)
        elseif field_number == 3
            early_due_date = PB.decode(d, Int64)
        elseif field_number == 4
            late_due_date = PB.decode(d, Int64)
        elseif field_number == 5
            earliness_cost_per_time_unit = PB.decode(d, Int64)
        elseif field_number == 6
            lateness_cost_per_time_unit = PB.decode(d, Int64)
        elseif field_number == 7
            PB.decode!(d, latest_end)
        elseif field_number == 16
            name = PB.decode(d, String)
        else
            PB.skip(d, wire_type)
        end
    end
    return Job(tasks[], earliest_start[], early_due_date, late_due_date, earliness_cost_per_time_unit, lateness_cost_per_time_unit, latest_end[], name)
end

function PB.encode(e::PB.AbstractProtoEncoder, x::Job)
    initpos = position(e.io)
    !isempty(x.tasks) && PB.encode(e, 1, x.tasks)
    !isnothing(x.earliest_start) && PB.encode(e, 2, x.earliest_start)
    x.early_due_date != zero(Int64) && PB.encode(e, 3, x.early_due_date)
    x.late_due_date != zero(Int64) && PB.encode(e, 4, x.late_due_date)
    x.earliness_cost_per_time_unit != zero(Int64) && PB.encode(e, 5, x.earliness_cost_per_time_unit)
    x.lateness_cost_per_time_unit != zero(Int64) && PB.encode(e, 6, x.lateness_cost_per_time_unit)
    !isnothing(x.latest_end) && PB.encode(e, 7, x.latest_end)
    !isempty(x.name) && PB.encode(e, 16, x.name)
    return position(e.io) - initpos
end
function PB._encoded_size(x::Job)
    encoded_size = 0
    !isempty(x.tasks) && (encoded_size += PB._encoded_size(x.tasks, 1))
    !isnothing(x.earliest_start) && (encoded_size += PB._encoded_size(x.earliest_start, 2))
    x.early_due_date != zero(Int64) && (encoded_size += PB._encoded_size(x.early_due_date, 3))
    x.late_due_date != zero(Int64) && (encoded_size += PB._encoded_size(x.late_due_date, 4))
    x.earliness_cost_per_time_unit != zero(Int64) && (encoded_size += PB._encoded_size(x.earliness_cost_per_time_unit, 5))
    x.lateness_cost_per_time_unit != zero(Int64) && (encoded_size += PB._encoded_size(x.lateness_cost_per_time_unit, 6))
    !isnothing(x.latest_end) && (encoded_size += PB._encoded_size(x.latest_end, 7))
    !isempty(x.name) && (encoded_size += PB._encoded_size(x.name, 16))
    return encoded_size
end

struct AssignedJob
    tasks::Vector{AssignedTask}
    due_date_cost::Int64
    sum_of_task_costs::Int64
end
PB.default_values(::Type{AssignedJob}) = (;tasks = Vector{AssignedTask}(), due_date_cost = zero(Int64), sum_of_task_costs = zero(Int64))
PB.field_numbers(::Type{AssignedJob}) = (;tasks = 1, due_date_cost = 2, sum_of_task_costs = 3)

function PB.decode(d::PB.AbstractProtoDecoder, ::Type{<:AssignedJob})
    tasks = PB.BufferedVector{AssignedTask}()
    due_date_cost = zero(Int64)
    sum_of_task_costs = zero(Int64)
    while !PB.message_done(d)
        field_number, wire_type = PB.decode_tag(d)
        if field_number == 1
            PB.decode!(d, tasks)
        elseif field_number == 2
            due_date_cost = PB.decode(d, Int64)
        elseif field_number == 3
            sum_of_task_costs = PB.decode(d, Int64)
        else
            PB.skip(d, wire_type)
        end
    end
    return AssignedJob(tasks[], due_date_cost, sum_of_task_costs)
end

function PB.encode(e::PB.AbstractProtoEncoder, x::AssignedJob)
    initpos = position(e.io)
    !isempty(x.tasks) && PB.encode(e, 1, x.tasks)
    x.due_date_cost != zero(Int64) && PB.encode(e, 2, x.due_date_cost)
    x.sum_of_task_costs != zero(Int64) && PB.encode(e, 3, x.sum_of_task_costs)
    return position(e.io) - initpos
end
function PB._encoded_size(x::AssignedJob)
    encoded_size = 0
    !isempty(x.tasks) && (encoded_size += PB._encoded_size(x.tasks, 1))
    x.due_date_cost != zero(Int64) && (encoded_size += PB._encoded_size(x.due_date_cost, 2))
    x.sum_of_task_costs != zero(Int64) && (encoded_size += PB._encoded_size(x.sum_of_task_costs, 3))
    return encoded_size
end

struct Machine
    transition_time_matrix::Union{Nothing,TransitionTimeMatrix}
    name::String
end
PB.default_values(::Type{Machine}) = (;transition_time_matrix = nothing, name = "")
PB.field_numbers(::Type{Machine}) = (;transition_time_matrix = 1, name = 16)

function PB.decode(d::PB.AbstractProtoDecoder, ::Type{<:Machine})
    transition_time_matrix = Ref{Union{Nothing,TransitionTimeMatrix}}(nothing)
    name = ""
    while !PB.message_done(d)
        field_number, wire_type = PB.decode_tag(d)
        if field_number == 1
            PB.decode!(d, transition_time_matrix)
        elseif field_number == 16
            name = PB.decode(d, String)
        else
            PB.skip(d, wire_type)
        end
    end
    return Machine(transition_time_matrix[], name)
end

function PB.encode(e::PB.AbstractProtoEncoder, x::Machine)
    initpos = position(e.io)
    !isnothing(x.transition_time_matrix) && PB.encode(e, 1, x.transition_time_matrix)
    !isempty(x.name) && PB.encode(e, 16, x.name)
    return position(e.io) - initpos
end
function PB._encoded_size(x::Machine)
    encoded_size = 0
    !isnothing(x.transition_time_matrix) && (encoded_size += PB._encoded_size(x.transition_time_matrix, 1))
    !isempty(x.name) && (encoded_size += PB._encoded_size(x.name, 16))
    return encoded_size
end

struct JsspOutputSolution
    jobs::Vector{AssignedJob}
    makespan_cost::Int64
    total_cost::Int64
end
PB.default_values(::Type{JsspOutputSolution}) = (;jobs = Vector{AssignedJob}(), makespan_cost = zero(Int64), total_cost = zero(Int64))
PB.field_numbers(::Type{JsspOutputSolution}) = (;jobs = 1, makespan_cost = 2, total_cost = 3)

function PB.decode(d::PB.AbstractProtoDecoder, ::Type{<:JsspOutputSolution})
    jobs = PB.BufferedVector{AssignedJob}()
    makespan_cost = zero(Int64)
    total_cost = zero(Int64)
    while !PB.message_done(d)
        field_number, wire_type = PB.decode_tag(d)
        if field_number == 1
            PB.decode!(d, jobs)
        elseif field_number == 2
            makespan_cost = PB.decode(d, Int64)
        elseif field_number == 3
            total_cost = PB.decode(d, Int64)
        else
            PB.skip(d, wire_type)
        end
    end
    return JsspOutputSolution(jobs[], makespan_cost, total_cost)
end

function PB.encode(e::PB.AbstractProtoEncoder, x::JsspOutputSolution)
    initpos = position(e.io)
    !isempty(x.jobs) && PB.encode(e, 1, x.jobs)
    x.makespan_cost != zero(Int64) && PB.encode(e, 2, x.makespan_cost)
    x.total_cost != zero(Int64) && PB.encode(e, 3, x.total_cost)
    return position(e.io) - initpos
end
function PB._encoded_size(x::JsspOutputSolution)
    encoded_size = 0
    !isempty(x.jobs) && (encoded_size += PB._encoded_size(x.jobs, 1))
    x.makespan_cost != zero(Int64) && (encoded_size += PB._encoded_size(x.makespan_cost, 2))
    x.total_cost != zero(Int64) && (encoded_size += PB._encoded_size(x.total_cost, 3))
    return encoded_size
end

struct JsspInputProblem
    jobs::Vector{Job}
    machines::Vector{Machine}
    precedences::Vector{JobPrecedence}
    makespan_cost_per_time_unit::Int64
    scaling_factor::Union{Nothing,google.protobuf.DoubleValue}
    seed::Int32
    name::String
end
PB.default_values(::Type{JsspInputProblem}) = (;jobs = Vector{Job}(), machines = Vector{Machine}(), precedences = Vector{JobPrecedence}(), makespan_cost_per_time_unit = zero(Int64), scaling_factor = nothing, seed = zero(Int32), name = "")
PB.field_numbers(::Type{JsspInputProblem}) = (;jobs = 1, machines = 2, precedences = 3, makespan_cost_per_time_unit = 4, scaling_factor = 5, seed = 24, name = 16)

function PB.decode(d::PB.AbstractProtoDecoder, ::Type{<:JsspInputProblem})
    jobs = PB.BufferedVector{Job}()
    machines = PB.BufferedVector{Machine}()
    precedences = PB.BufferedVector{JobPrecedence}()
    makespan_cost_per_time_unit = zero(Int64)
    scaling_factor = Ref{Union{Nothing,google.protobuf.DoubleValue}}(nothing)
    seed = zero(Int32)
    name = ""
    while !PB.message_done(d)
        field_number, wire_type = PB.decode_tag(d)
        if field_number == 1
            PB.decode!(d, jobs)
        elseif field_number == 2
            PB.decode!(d, machines)
        elseif field_number == 3
            PB.decode!(d, precedences)
        elseif field_number == 4
            makespan_cost_per_time_unit = PB.decode(d, Int64)
        elseif field_number == 5
            PB.decode!(d, scaling_factor)
        elseif field_number == 24
            seed = PB.decode(d, Int32)
        elseif field_number == 16
            name = PB.decode(d, String)
        else
            PB.skip(d, wire_type)
        end
    end
    return JsspInputProblem(jobs[], machines[], precedences[], makespan_cost_per_time_unit, scaling_factor[], seed, name)
end

function PB.encode(e::PB.AbstractProtoEncoder, x::JsspInputProblem)
    initpos = position(e.io)
    !isempty(x.jobs) && PB.encode(e, 1, x.jobs)
    !isempty(x.machines) && PB.encode(e, 2, x.machines)
    !isempty(x.precedences) && PB.encode(e, 3, x.precedences)
    x.makespan_cost_per_time_unit != zero(Int64) && PB.encode(e, 4, x.makespan_cost_per_time_unit)
    !isnothing(x.scaling_factor) && PB.encode(e, 5, x.scaling_factor)
    x.seed != zero(Int32) && PB.encode(e, 24, x.seed)
    !isempty(x.name) && PB.encode(e, 16, x.name)
    return position(e.io) - initpos
end
function PB._encoded_size(x::JsspInputProblem)
    encoded_size = 0
    !isempty(x.jobs) && (encoded_size += PB._encoded_size(x.jobs, 1))
    !isempty(x.machines) && (encoded_size += PB._encoded_size(x.machines, 2))
    !isempty(x.precedences) && (encoded_size += PB._encoded_size(x.precedences, 3))
    x.makespan_cost_per_time_unit != zero(Int64) && (encoded_size += PB._encoded_size(x.makespan_cost_per_time_unit, 4))
    !isnothing(x.scaling_factor) && (encoded_size += PB._encoded_size(x.scaling_factor, 5))
    x.seed != zero(Int32) && (encoded_size += PB._encoded_size(x.seed, 24))
    !isempty(x.name) && (encoded_size += PB._encoded_size(x.name, 16))
    return encoded_size
end
