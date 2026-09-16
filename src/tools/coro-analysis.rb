#!/usr/bin/env ruby
# frozen_string_literal: true

require 'set'

module Color
  RESET   = "\e[0m"
  BOLD    = "\e[1m"
  DIM     = "\e[2m"
  RED     = "\e[31m"
  GREEN   = "\e[32m"
  YELLOW  = "\e[33m"
  BLUE    = "\e[34m"
  MAGENTA = "\e[35m"
  CYAN    = "\e[36m"
  WHITE   = "\e[37m"

  def self.bold(s); "#{BOLD}#{s}#{RESET}"; end
  def self.dim(s); "#{DIM}#{s}#{RESET}"; end
  def self.red(s); "#{RED}#{s}#{RESET}"; end
  def self.green(s); "#{GREEN}#{s}#{RESET}"; end
  def self.yellow(s); "#{YELLOW}#{s}#{RESET}"; end
  def self.blue(s); "#{BLUE}#{s}#{RESET}"; end
  def self.magenta(s); "#{MAGENTA}#{s}#{RESET}"; end
  def self.cyan(s); "#{CYAN}#{s}#{RESET}"; end
end

if ARGV.empty? || ARGV.include?('-h') || ARGV.include?('--help')
  puts "#{Color.bold("Usage:")} ruby coro-analysis.rb <trace.log|stdin> [sched_id:coro_id|coro_ptr|coro_name]"
  puts "Analyzes coroutine lifecycle, cross-scheduler/thread interactions, leaks, and anomalies."
  exit(0)
end

log_file = ARGV[0]
target_filter = ARGV[1]

coros = {}
schedulers = {}
threads = {}
timeline = []
anomalies = []

lines = if log_file == '-'
          $stdin.readlines(chomp: true)
        else
          File.readlines(log_file, chomp: true)
        end

lines.each_with_index do |line, line_num|
  next unless line.include?('coro(')

  timestamp = nil
  timestamp = $1.to_f if line =~ /^\[(\d+\.\d+)\]/

  tid = nil
  tid = $1.to_i if line =~ /\[tid=(\d+)\]/

  sched_id = 0
  sched_id = $1.to_i if line =~ /\[sched=(-?\d+)\]/

  idx_open = line.index('coro(')
  idx_close = line.index(')', idx_open)
  next unless idx_open && idx_close

  body = line[(idx_open + 5)...idx_close].strip
  at_idx = line.index('@', idx_close)
  call_loc = at_idx ? line[(at_idx + 1)..-1].strip : "unknown"

  parts = body.split(':', 2)
  tag = parts[0].strip
  info = parts[1] ? parts[1].strip : ""

  if info == "(null)"
    timeline << { tag: tag, coro_id: -1, sched_id: sched_id, name: "null", ptr: "0x0", loc: call_loc, line_num: line_num + 1, tid: tid }
    next
  end

  name = "anon"
  id = -1
  ptr = "0x0"
  origin = "unknown"

  name = $1 if info =~ /'([^']*)'/
  id = $1.to_i if info =~ /id=(-?\d+)/ || info =~ /:(-?\d+)\b/
  ptr = $1 if info =~ /ptr=(0x[0-9a-fA-F]+)/ || info =~ /at (0x[0-9a-fA-F]+)/
  origin = $1 if info =~ /origin=([^ ]+)/

  key = if id >= 0
          "#{sched_id}:#{id}"
        elsif ptr != "0x0"
          ptr
        else
          "#{sched_id}:#{name}"
        end
  next if key == "0x0"

  unless coros.key?(key)
    coros[key] = {
      key: key,
      id: id,
      sched_id: sched_id,
      name: name,
      ptr: ptr,
      origin: origin,
      spawn_tid: tid,
      spawn_sched: sched_id,
      tids: Set[],
      scheds: Set[],
      spawn_loc: nil,
      exit_loc: nil,
      spawn_time: timestamp,
      exit_time: nil,
      spawns: 0,
      suspends: 0,
      resumes: 0,
      switches: 0,
      wakes: 0,
      exits: 0,
      state: "Created",
      events: []
    }
  end

  rec = coros[key]
  rec[:name] = name if name != "anon" && rec[:name] == "anon"
  rec[:origin] = origin if origin != "unknown" && rec[:origin] == "unknown"
  rec[:ptr] = ptr if ptr != "0x0" && rec[:ptr] == "0x0"
  rec[:tids].add(tid) if tid
  rec[:scheds].add(sched_id)

  schedulers[sched_id] ||= {
    tid: tid,
    spawns: 0,
    suspends: 0,
    resumes: 0,
    switches: 0,
    wakes_sent: 0,
    exits: 0,
    coros: Set[]
  }
  s_rec = schedulers[sched_id]
  s_rec[:coros].add(key)
  s_rec[:tid] = tid if tid
  case tag
  when "spawn"   then s_rec[:spawns] += 1
  when "suspend" then s_rec[:suspends] += 1
  when "resume"  then s_rec[:resumes] += 1
  when "switch"  then s_rec[:switches] += 1
  when "wake"    then s_rec[:wakes_sent] += 1
  when "exit"    then s_rec[:exits] += 1
  end

  if tid
    threads[tid] ||= Set[]
    threads[tid].add(sched_id)
  end

  event_entry = {
    tag: tag,
    time: timestamp,
    loc: call_loc,
    line_num: line_num + 1,
    tid: tid,
    sched_id: sched_id
  }
  rec[:events] << event_entry
  timeline << event_entry.merge(coro_id: id, name: name, ptr: ptr)

  case tag
  when "spawn"
    rec[:spawns] += 1
    rec[:spawn_loc] = call_loc
    rec[:spawn_time] ||= timestamp
    rec[:spawn_tid] ||= tid
    rec[:spawn_sched] ||= sched_id
    rec[:state] = "Running"
  when "suspend"
    rec[:suspends] += 1
    rec[:state] = "Suspended"
  when "resume"
    rec[:resumes] += 1
    rec[:state] = "Ready"
  when "switch"
    rec[:switches] += 1
    rec[:state] = "Running"
  when "wake"
    rec[:wakes] += 1
    rec[:state] = "Ready (Woken)"
  when "exit"
    rec[:exits] += 1
    rec[:exit_loc] = call_loc
    rec[:exit_time] = timestamp
    if rec[:state] == "Exited"
      anomalies << "Double exit detected for Coroutine #{key} ('#{name}', #{ptr}) at line #{line_num + 1}"
    end
    rec[:state] = "Exited"
  end
end

def format_tag(tag)
  case tag
  when "spawn"   then Color.green(tag.to_s.ljust(8))
  when "suspend" then Color.yellow(tag.to_s.ljust(8))
  when "resume"  then Color.cyan(tag.to_s.ljust(8))
  when "switch"  then Color.blue(tag.to_s.ljust(8))
  when "wake"    then Color.magenta(tag.to_s.ljust(8))
  when "exit"    then Color.red(tag.to_s.ljust(8))
  else Color.dim(tag.to_s.ljust(8))
  end
end

def format_state(state)
  case state
  when "Exited"        then Color.green(state)
  when "Suspended"     then Color.yellow(state)
  when "Ready (Woken)" then Color.magenta(state)
  when "Ready"         then Color.cyan(state)
  when "Running"       then Color.blue(state)
  else Color.red(state)
  end
end

puts Color.cyan("=" * 80)
puts Color.bold(Color.cyan("                        COROUTINE TRACE ANALYSIS REPORT"))
puts Color.cyan("=" * 80)

total_coros = coros.size
exited_coros = coros.count { |_, c| c[:state] == "Exited" }
active_coros = coros.count { |_, c| c[:state] != "Exited" }
total_switches = coros.sum { |_, c| c[:switches] }
total_wakes = coros.sum { |_, c| c[:wakes] }
cross_sched_coros = coros.select { |_, c| c[:scheds].size > 1 || c[:tids].size > 1 }

puts "#{Color.bold("Total Tracked Coroutines :")} #{Color.bold(Color.cyan(total_coros.to_s))}"
puts "#{Color.bold("Total Schedulers         :")} #{Color.bold(Color.magenta(schedulers.size.to_s))} (across #{threads.size} OS threads)"
puts "#{Color.bold("Exited Cleanly           :")} #{Color.green(exited_coros.to_s)}"
if active_coros > 0
  puts "#{Color.bold("Active / Unfinished      :")} #{Color.bold(Color.red(active_coros.to_s))}"
else
  puts "#{Color.bold("Active / Unfinished      :")} #{Color.green("0")}"
end
puts "#{Color.bold("Total Context Switches   :")} #{Color.blue(total_switches.to_s)}"
puts "#{Color.bold("Cross-Scheduler Coros    :")} #{cross_sched_coros.empty? ? Color.green("0") : Color.bold(Color.yellow(cross_sched_coros.size.to_s))}"
puts "#{Color.bold("Cross-Thread Wakeups     :")} #{Color.magenta(total_wakes.to_s)}"
puts Color.dim("-" * 80)

# Per-Scheduler Breakdown
puts "\n#{Color.bold(Color.cyan("PER-SCHEDULER BREAKDOWN (#{schedulers.size} schedulers):"))}"
schedulers.sort.each do |sched_id, s|
  puts "  • #{Color.bold(Color.magenta("[sched=#{sched_id}]"))} #{Color.dim("[tid=#{s[:tid]}]")} Coroutines: #{Color.cyan(s[:coros].size.to_s)} | Switches: #{Color.blue(s[:switches].to_s)} | Suspends: #{Color.yellow(s[:suspends].to_s)} | Resumes: #{Color.cyan(s[:resumes].to_s)} | Wakes Sent: #{Color.magenta(s[:wakes_sent].to_s)}"
end

# Cross-Scheduler Coroutines
if cross_sched_coros.size > 0
  puts "\n#{Color.bold(Color.yellow("[⇄] CROSS-SCHEDULER / THREAD MIGRATIONS (#{cross_sched_coros.size}):"))}"
  cross_sched_coros.each do |_, c|
    scheds_str = c[:scheds].to_a.map { |s| "[sched=#{s}]" }.join(", ")
    tids_str = c[:tids].to_a.map { |t| "[tid=#{t}]" }.join(", ")
    puts "  • #{Color.cyan("[#{c[:key]}]")} #{Color.bold("'#{c[:name]}'")} #{Color.dim("(#{c[:ptr]})")} Spawned on: #{Color.magenta("[sched=#{c[:spawn_sched]}] [tid=#{c[:spawn_tid]}]")}"
    puts "    Active across schedulers: #{Color.magenta(scheds_str)} | Threads: #{Color.magenta(tids_str)}"
    puts "    Origin: #{Color.blue(c[:origin])}"
    puts "    Events: spawns=#{c[:spawns]}, switches=#{c[:switches]}, wakes=#{c[:wakes]}, suspends=#{c[:suspends]}, resumes=#{c[:resumes]}"
  end
end

if active_coros > 0
  puts "\n#{Color.bold(Color.red("[!] UNFINISHED / LEAKED COROUTINES (#{active_coros}):"))}"
  coros.each do |_, c|
    next if c[:state] == "Exited"
    scheds_str = c[:scheds].to_a.map { |s| "[sched=#{s}]" }.join(", ")
    tids_str = c[:tids].to_a.map { |t| "[tid=#{t}]" }.join(", ")
    puts "  • #{Color.cyan("[#{c[:key]}]")} #{Color.bold("'#{c[:name]}'")} #{Color.dim("(#{c[:ptr]})")} Schedulers: #{Color.magenta(scheds_str)} Threads: #{Color.magenta(tids_str)} State: #{format_state(c[:state])}"
    puts "    #{Color.bold("Origin")}: #{Color.blue(c[:origin])}"
    puts "    #{Color.bold("Stats ")}: suspends=#{Color.yellow(c[:suspends].to_s)}, resumes=#{Color.cyan(c[:resumes].to_s)}, switches=#{Color.blue(c[:switches].to_s)}, wakes=#{Color.magenta(c[:wakes].to_s)}"
    puts "    #{Color.bold("Last  ")}: #{c[:events].last ? "#{format_tag(c[:events].last[:tag])} #{Color.dim("@")} #{Color.dim(c[:events].last[:loc])}" : Color.dim("none")}"
  end
end

unless anomalies.empty?
  puts "\n#{Color.bold(Color.red("[!] ANOMALIES DETECTED (#{anomalies.size}):"))}"
  anomalies.each { |a| puts "  • #{Color.red(a)}" }
end

if target_filter
  match = coros.find do |k, c|
    k.to_s == target_filter ||
      c[:key] == target_filter ||
      c[:id].to_s == target_filter ||
      c[:ptr] == target_filter ||
      c[:name] == target_filter
  end
  if match
    _, c = match
    scheds_str = c[:scheds].to_a.map { |s| "[sched=#{s}]" }.join(", ")
    tids_str = c[:tids].to_a.map { |t| "[tid=#{t}]" }.join(", ")
    puts "\n" + Color.cyan("=" * 80)
    puts Color.bold("DETAILS FOR COROUTINE #{c[:key]} ('#{c[:name]}', #{c[:ptr]}):")
    puts "  #{Color.bold("Origin     :")} #{Color.blue(c[:origin])}"
    puts "  #{Color.bold("Schedulers :")} #{Color.magenta(scheds_str)}"
    puts "  #{Color.bold("Threads    :")} #{Color.magenta(tids_str)}"
    puts "  #{Color.bold("Final State:")} #{format_state(c[:state])}"
    puts "  #{Color.bold("Lifecycle  :")} spawns=#{Color.green(c[:spawns].to_s)}, suspends=#{Color.yellow(c[:suspends].to_s)}, resumes=#{Color.cyan(c[:resumes].to_s)}, switches=#{Color.blue(c[:switches].to_s)}, wakes=#{Color.magenta(c[:wakes].to_s)}, exits=#{Color.red(c[:exits].to_s)}"
    puts "\n  #{Color.bold("Event History:")}"
    c[:events].each do |ev|
      t_str = ev[:time] ? sprintf("%.6f", ev[:time]) : "  --.------"
      tid_str = ev[:tid] ? "[tid=#{ev[:tid]}]" : "[tid=------]"
      sched_str = "[sched=#{ev[:sched_id]}]"
      puts "    #{Color.dim("[#{t_str}]")} #{Color.magenta(tid_str)} #{Color.magenta(sched_str)} #{Color.dim("[line #{ev[:line_num].to_s.rjust(5)}]")} #{format_tag(ev[:tag])} #{Color.dim("@")} #{Color.dim(ev[:loc])}"
    end
  else
    puts "\n#{Color.yellow("[?] No coroutine found matching '#{target_filter}'")}"
  end
end
puts Color.cyan("=" * 80)
