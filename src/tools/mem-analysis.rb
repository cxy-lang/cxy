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
  puts "#{Color.bold("Usage:")} ruby mem-analysis.rb <trace.log|stdin> [ptr_address]"
  puts "Analyzes reference-counted memory lifecycles, leaks, double-frees, and cross-thread access."
  exit(0)
end

log_file = ARGV[0]
target_filter = ARGV[1]

# State keyed by pointer address
active_by_ptr = {}   # ptr -> current active Allocation
all_allocations = [] # list of all Allocation objects
threads = {}         # tid -> stats
anomalies = []

class Allocation
  attr_accessor :id, :ptr, :epoch, :alloc_loc, :alloc_tid, :alloc_time,
                :freed, :freed_loc, :freed_tid, :freed_time,
                :refs, :gets, :drops, :tids, :events

  def initialize(id, ptr, epoch, loc, tid, time)
    @id = id
    @ptr = ptr
    @epoch = epoch
    @alloc_loc = loc
    @alloc_tid = tid
    @alloc_time = time
    @freed = false
    @freed_loc = nil
    @freed_tid = nil
    @freed_time = nil
    @refs = 1
    @gets = 0
    @drops = 0
    @tids = Set[]
    @tids.add(tid) if tid
    @events = []
  end
end

lines = if log_file == '-'
          $stdin.readlines(chomp: true)
        else
          File.readlines(log_file, chomp: true)
        end

alloc_counter = 0
ptr_epochs = Hash.new(0)

lines.each_with_index do |line, line_num|
  if line.include?('[DOUBLE FREE DETECTED]')
    anomalies << line.strip
    next
  end

  if line.include?('[MEM LEAK]')
    next
  end

  next unless line.include?('mem(')

  timestamp = nil
  timestamp = $1.to_f if line =~ /^\[(\d+\.\d+)\]/

  tid = nil
  tid = $1.to_i if line =~ /\[tid=(\d+)\]/

  idx_open = line.index('mem(')
  idx_close = line.index(')', idx_open)
  next unless idx_open && idx_close

  body = line[(idx_open + 4)...idx_close].strip
  at_idx = line.index('@', idx_close)
  call_loc = at_idx ? line[(at_idx + 1)..-1].strip : "unknown"

  parts = body.split(':', 2)
  tag = parts[0].strip.downcase
  addr_part = parts[1] ? parts[1].strip : ""

  if addr_part =~ /\[tid=(\d+)\]\s*(0x[0-9a-fA-F]+)/
    tid ||= $1.to_i
    addr = $2
  elsif addr_part =~ /(0x[0-9a-fA-F]+)/
    addr = $1
  else
    addr = addr_part
  end

  next if addr == "0x0" || addr.empty?

  if tid
    threads[tid] ||= {
      allocs: 0,
      gets: 0,
      drops: 0,
      frees: 0,
      ptrs: Set[]
    }
    t_rec = threads[tid]
    t_rec[:ptrs].add(addr)
    case tag
    when "alloc" then t_rec[:allocs] += 1
    when "get"   then t_rec[:gets] += 1
    when "drop"  then t_rec[:drops] += 1
    when "freed" then t_rec[:frees] += 1
    end
  end

  event_data = {
    tag: tag,
    time: timestamp,
    tid: tid,
    loc: call_loc,
    line_num: line_num + 1
  }

  case tag
  when "alloc"
    epoch = (ptr_epochs[addr] += 1)
    alloc_counter += 1
    alloc = Allocation.new(alloc_counter, addr, epoch, call_loc, tid, timestamp)
    alloc.events << event_data
    active_by_ptr[addr] = alloc
    all_allocations << alloc

  when "get"
    alloc = active_by_ptr[addr]
    if alloc.nil?
      anomalies << "Use-after-free (get on unallocated/freed ptr) #{addr} @ #{call_loc} (line #{line_num + 1})"
    else
      alloc.gets += 1
      alloc.refs += 1
      alloc.tids.add(tid) if tid
      alloc.events << event_data
    end

  when "drop"
    alloc = active_by_ptr[addr]
    if alloc.nil?
      anomalies << "Use-after-free (drop on unallocated/freed ptr) #{addr} @ #{call_loc} (line #{line_num + 1})"
    else
      alloc.drops += 1
      alloc.refs -= 1
      alloc.tids.add(tid) if tid
      alloc.events << event_data
      if alloc.refs < 0
        anomalies << "Negative refcount (#{alloc.refs}) on #{addr} @ #{call_loc} (line #{line_num + 1})"
      end
    end

  when "freed"
    alloc = active_by_ptr[addr]
    if alloc.nil?
      anomalies << "Double free or untracked free detected on #{addr} @ #{call_loc} (line #{line_num + 1})"
    else
      alloc.freed = true
      alloc.freed_loc = call_loc
      alloc.freed_tid = tid
      alloc.freed_time = timestamp
      alloc.refs = 0
      alloc.events << event_data
      active_by_ptr.delete(addr)
    end
  end
end

def format_tag(tag)
  case tag
  when "alloc" then Color.green(tag.to_s.ljust(6))
  when "get"   then Color.cyan(tag.to_s.ljust(6))
  when "drop"  then Color.yellow(tag.to_s.ljust(6))
  when "freed" then Color.red(tag.to_s.ljust(6))
  else Color.dim(tag.to_s.ljust(6))
  end
end

puts Color.cyan("=" * 80)
puts Color.bold(Color.cyan("                         MEMORY TRACE ANALYSIS REPORT"))
puts Color.cyan("=" * 80)

total_allocs = all_allocations.size
freed_allocs = all_allocations.count(&:freed)
leaked_allocs = all_allocations.reject(&:freed)
total_gets = all_allocations.sum(&:gets)
total_drops = all_allocations.sum(&:drops)
cross_thread_allocs = all_allocations.select { |a| a.tids.size > 1 }

puts "#{Color.bold("Total Allocations        :")} #{Color.bold(Color.cyan(total_allocs.to_s))}"
puts "#{Color.bold("Total Active Threads     :")} #{Color.bold(Color.magenta(threads.size.to_s))}"
puts "#{Color.bold("Freed Cleanly            :")} #{Color.green(freed_allocs.to_s)}"
if leaked_allocs.empty?
  puts "#{Color.bold("Active / Leaked Blocks   :")} #{Color.green("0")}"
else
  puts "#{Color.bold("Active / Leaked Blocks   :")} #{Color.bold(Color.red(leaked_allocs.size.to_s))}"
end
puts "#{Color.bold("Ref Incs (get) / Decs (drop):")} #{Color.cyan(total_gets.to_s)} / #{Color.yellow(total_drops.to_s)}"
puts "#{Color.bold("Cross-Thread Allocations :")} #{cross_thread_allocs.empty? ? Color.green("0") : Color.bold(Color.yellow(cross_thread_allocs.size.to_s))}"
puts Color.dim("-" * 80)

# Per-Thread Breakdown
if threads.size > 0
  puts "\n#{Color.bold(Color.cyan("PER-THREAD MEMORY BREAKDOWN (#{threads.size} threads):"))}"
  threads.sort.each do |tid, t|
    puts "  • #{Color.bold(Color.magenta("[tid=#{tid}]"))} Ptrs: #{Color.cyan(t[:ptrs].size.to_s)} | Allocs: #{Color.green(t[:allocs].to_s)} | Gets: #{Color.cyan(t[:gets].to_s)} | Drops: #{Color.yellow(t[:drops].to_s)} | Frees: #{Color.red(t[:frees].to_s)}"
  end
end

# Leaked Summary Grouped by Code Location
if leaked_allocs.size > 0
  puts "\n#{Color.bold(Color.red("[!] LEAKED ALLOCATIONS GROUPED BY CODE LOCATION (#{leaked_allocs.size} blocks):"))}"
  leaks_by_loc = Hash.new { |h, k| h[k] = { count: 0, total_refs: 0, allocs: [] } }
  leaked_allocs.each do |a|
    leaks_by_loc[a.alloc_loc][:count] += 1
    leaks_by_loc[a.alloc_loc][:total_refs] += a.refs
    leaks_by_loc[a.alloc_loc][:allocs] << a
  end

  leaks_by_loc.sort_by { |_, v| -v[:count] }.each do |loc, data|
    puts "\n  #{Color.bold(Color.red("• #{data[:count]} leak#{data[:count] > 1 ? 's' : ''}"))} #{Color.dim("(total active refs=#{data[:total_refs]})")} @ #{Color.blue(loc)}"
    data[:allocs].each do |a|
      tids_str = a.tids.to_a.map { |t| "[tid=#{t}]" }.join(", ")
      puts "    - #{Color.bold(Color.cyan(a.ptr))} #{Color.yellow("refs=#{a.refs}")} #{Color.dim("alloc_tid=#{a.alloc_tid}")} #{Color.dim("threads: #{tids_str}")}"
    end
  end
end

# Anomalies
unless anomalies.empty?
  puts "\n#{Color.bold(Color.red("[!] MEMORY ANOMALIES DETECTED (#{anomalies.size}):"))}"
  anomalies.each { |a| puts "  • #{Color.red(a)}" }
end

# Target Filter Drill-down
if target_filter
  matched = all_allocations.select { |a| a.ptr == target_filter || a.ptr.end_with?(target_filter) }
  if matched.size > 0
    puts "\n" + Color.cyan("=" * 80)
    puts Color.bold("LIFECYCLE HISTORY FOR #{target_filter} (#{matched.size} allocation epoch#{matched.size > 1 ? 's' : ''}):")
    matched.each_with_index do |a, idx|
      tids_str = a.tids.to_a.map { |t| "[tid=#{t}]" }.join(", ")
      puts "\n  #{Color.bold("Epoch ##{a.epoch} (Alloc ##{a.id}):")}"
      puts "    #{Color.bold("Pointer   :")} #{Color.cyan(a.ptr)}"
      puts "    #{Color.bold("Allocated @")} #{Color.blue(a.alloc_loc)} #{Color.dim("[tid=#{a.alloc_tid}]")}"
      puts "    #{Color.bold("Status    :")} #{a.freed ? Color.green("Freed @ #{a.freed_loc} [tid=#{a.freed_tid}]") : Color.red("Active / Leaked (refs=#{a.refs})")}"
      puts "    #{Color.bold("Threads   :")} #{Color.magenta(tids_str)}"
      puts "    #{Color.bold("Stats     :")} gets=#{a.gets}, drops=#{a.drops}"
      puts "    #{Color.bold("Timeline  :")}"
      a.events.each do |ev|
        t_str = ev[:time] ? sprintf("%.6f", ev[:time]) : "  --.------"
        tid_str = ev[:tid] ? "[tid=#{ev[:tid]}]" : "[tid=------]"
        puts "      #{Color.dim("[#{t_str}]")} #{Color.magenta(tid_str)} #{Color.dim("[line #{ev[:line_num].to_s.rjust(5)}]")} #{format_tag(ev[:tag])} #{Color.dim("@")} #{Color.dim(ev[:loc])}"
      end
    end
  else
    puts "\n#{Color.yellow("[?] No memory allocation found matching '#{target_filter}'")}"
  end
end
puts Color.cyan("=" * 80)
