# Duration System Specification for stdlib/time.cxy

## Overview
Add type-safe duration types with automatic unit conversion, enums for days/months, and Time arithmetic operators to `stdlib/time.cxy`.

---

## 1. Add Date Format Macros

Add after existing `HTTP_TIME_FMT` and `LOG_TIME_FMT` macros:

```cxy
macro DATE_FMT "%Y-%m-%d"
macro TIME_FMT "%H:%M:%S"
macro DATETIME_FMT "%Y-%m-%d %H:%M:%S"
```

---

## 2. Enumerations

Add before duration types:

```cxy
pub enum DayOfWeek {
    @str("Sunday")
    Sunday = 0,
    @str("Monday")
    Monday = 1,
    @str("Tuesday")
    Tuesday = 2,
    @str("Wednesday")
    Wednesday = 3,
    @str("Thursday")
    Thursday = 4,
    @str("Friday")
    Friday = 5,
    @str("Saturday")
    Saturday = 6
}

pub enum Month {
    @str("January")
    January = 1,
    @str("February")
    February = 2,
    @str("March")
    March = 3,
    @str("April")
    April = 4,
    @str("May")
    May = 5,
    @str("June")
    June = 6,
    @str("July")
    July = 7,
    @str("August")
    August = 8,
    @str("September")
    September = 9,
    @str("October")
    October = 10,
    @str("November")
    November = 11,
    @str("December")
    December = 12
}
```

---

## 3. Duration Types

Add these duration structs before the `Time` struct:

```cxy
pub struct Years {
    value: i64
    
    const func `as`[Unit](): Unit {
        #if Unit == #Years {
            return Years{value: value}
        }
        else #if Unit == #Months {
            return Months{value: value * 12}
        }
        else #if Unit == #Days {
            return Days{value: value * 365}
        }
        else #if Unit == #Hours {
            return Hours{value: value * 8760}
        }
        else #if Unit == #Minutes {
            return Minutes{value: value * 525600}
        }
        else #if Unit == #Seconds {
            return Seconds{value: value * 31536000}
        }
        else #if Unit == #Millis {
            return Millis{value: value * 31536000000}
        }
        else #if Unit == #i64 {
            return value
        }
    }
    
    const func `str`(os: &OutputStream) {
        os << value << " year" << (value == 1 ? "" : "s")
    }
}

pub struct Months {
    value: i64
    
    const func `as`[Unit](): Unit {
        #if Unit == #Years {
            return Years{value: value / 12}
        }
        else #if Unit == #Months {
            return Months{value: value}
        }
        else #if Unit == #Days {
            return Days{value: value * 30}
        }
        else #if Unit == #Hours {
            return Hours{value: value * 720}
        }
        else #if Unit == #Minutes {
            return Minutes{value: value * 43200}
        }
        else #if Unit == #Seconds {
            return Seconds{value: value * 2592000}
        }
        else #if Unit == #Millis {
            return Millis{value: value * 2592000000}
        }
        else #if Unit == #i64 {
            return value
        }
    }
    
    const func `str`(os: &OutputStream) {
        os << value << " month" << (value == 1 ? "" : "s")
    }
}

pub struct Days {
    value: i64
    
    const func `as`[Unit](): Unit {
        #if Unit == #Years {
            return Years{value: value / 365}
        }
        else #if Unit == #Months {
            return Months{value: value / 30}
        }
        else #if Unit == #Days {
            return Days{value: value}
        }
        else #if Unit == #Hours {
            return Hours{value: value * 24}
        }
        else #if Unit == #Minutes {
            return Minutes{value: value * 1440}
        }
        else #if Unit == #Seconds {
            return Seconds{value: value * 86400}
        }
        else #if Unit == #Millis {
            return Millis{value: value * 86400000}
        }
        else #if Unit == #i64 {
            return value
        }
    }
    
    const func `str`(os: &OutputStream) {
        os << value << " day" << (value == 1 ? "" : "s")
    }
}

pub struct Hours {
    value: i64
    
    const func `as`[Unit](): Unit {
        #if Unit == #Years {
            return Years{value: value / 8760}
        }
        else #if Unit == #Months {
            return Months{value: value / 720}
        }
        else #if Unit == #Days {
            return Days{value: value / 24}
        }
        else #if Unit == #Hours {
            return Hours{value: value}
        }
        else #if Unit == #Minutes {
            return Minutes{value: value * 60}
        }
        else #if Unit == #Seconds {
            return Seconds{value: value * 3600}
        }
        else #if Unit == #Millis {
            return Millis{value: value * 3600000}
        }
        else #if Unit == #i64 {
            return value
        }
    }
    
    const func `str`(os: &OutputStream) {
        os << value << " hour" << (value == 1 ? "" : "s")
    }
}

pub struct Minutes {
    value: i64
    
    const func `as`[Unit](): Unit {
        #if Unit == #Years {
            return Years{value: value / 525600}
        }
        else #if Unit == #Months {
            return Months{value: value / 43200}
        }
        else #if Unit == #Days {
            return Days{value: value / 1440}
        }
        else #if Unit == #Hours {
            return Hours{value: value / 60}
        }
        else #if Unit == #Minutes {
            return Minutes{value: value}
        }
        else #if Unit == #Seconds {
            return Seconds{value: value * 60}
        }
        else #if Unit == #Millis {
            return Millis{value: value * 60000}
        }
        else #if Unit == #i64 {
            return value
        }
    }
    
    const func `str`(os: &OutputStream) {
        os << value << " minute" << (value == 1 ? "" : "s")
    }
}

pub struct Seconds {
    value: i64
    
    const func `as`[Unit](): Unit {
        #if Unit == #Years {
            return Years{value: value / 31536000}
        }
        else #if Unit == #Months {
            return Months{value: value / 2592000}
        }
        else #if Unit == #Days {
            return Days{value: value / 86400}
        }
        else #if Unit == #Hours {
            return Hours{value: value / 3600}
        }
        else #if Unit == #Minutes {
            return Minutes{value: value / 60}
        }
        else #if Unit == #Seconds {
            return Seconds{value: value}
        }
        else #if Unit == #Millis {
            return Millis{value: value * 1000}
        }
        else #if Unit == #i64 {
            return value
        }
    }
    
    const func `str`(os: &OutputStream) {
        os << value << " second" << (value == 1 ? "" : "s")
    }
}

pub struct Millis {
    value: i64
    
    const func `as`[Unit](): Unit {
        #if Unit == #Years {
            return Years{value: value / 31536000000}
        }
        else #if Unit == #Months {
            return Months{value: value / 2592000000}
        }
        else #if Unit == #Days {
            return Days{value: value / 86400000}
        }
        else #if Unit == #Hours {
            return Hours{value: value / 3600000}
        }
        else #if Unit == #Minutes {
            return Minutes{value: value / 60000}
        }
        else #if Unit == #Seconds {
            return Seconds{value: value / 1000}
        }
        else #if Unit == #Millis {
            return Millis{value: value}
        }
        else #if Unit == #i64 {
            return value
        }
    }
    
    const func `str`(os: &OutputStream) {
        os << value << " millisecond" << (value == 1 ? "" : "s")
    }
}
```

---

## 4. Time Struct Enhancements

Add these methods to the existing `Time` struct:

### Conversion Methods

```cxy
const func `as`[Unit](): Unit {
    #if Unit == #Years {
        return Years{value: _t / 31536000}
    }
    else #if Unit == #Months {
        return Months{value: _t / 2592000}
    }
    else #if Unit == #Days {
        return Days{value: _t / 86400}
    }
    else #if Unit == #Hours {
        return Hours{value: _t / 3600}
    }
    else #if Unit == #Minutes {
        return Minutes{value: _t / 60}
    }
    else #if Unit == #Seconds {
        return Seconds{value: _t}
    }
    else #if Unit == #Millis {
        return Millis{value: _t * 1000}
    }
    else #if Unit == #i64 {
        return _t
    }
}
```

### Formatting Methods

```cxy
const func formatDate(): String {
    var output = String()
    format(DATE_FMT!, &output)
    return &&output
}

const func formatTime(): String {
    var output = String()
    format(TIME_FMT!, &output)
    return &&output
}

const func formatDateTime(): String {
    var output = String()
    format(DATETIME_FMT!, &output)
    return &&output
}
```

### Component Access Methods

```cxy
const func year(): i32 {
    return _tm.tm_year + 1900
}

const func month(): Month {
    return (_tm.tm_mon + 1) as Month
}

const func monthValue(): i32 {
    return _tm.tm_mon + 1
}

const func dayOfMonth(): i32 {
    return _tm.tm_mday
}

const func dayOfWeek(): DayOfWeek {
    return _tm.tm_wday as DayOfWeek
}

const func dayOfWeekValue(): i32 {
    return _tm.tm_wday
}

const func hour(): i32 {
    return _tm.tm_hour
}

const func minute(): i32 {
    return _tm.tm_min
}

const func second(): i32 {
    return _tm.tm_sec
}

const func dayOfYear(): i32 {
    return _tm.tm_yday + 1
}
```

### Boolean Check Methods

```cxy
const func isWeekend(): bool {
    return _tm.tm_wday == 0 || _tm.tm_wday == 6
}

const func isWeekday(): bool {
    return _tm.tm_wday >= 1 && _tm.tm_wday <= 5
}

const func isLeapYear(): bool {
    var y = year()
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)
}

const func isToday(): bool {
    var now = Time()
    return year() == now.year() && 
           monthValue() == now.monthValue() && 
           dayOfMonth() == now.dayOfMonth()
}

const func isFuture(): bool {
    return _t > Time()._t
}

const func isPast(): bool {
    return _t < Time()._t
}
```

### Arithmetic Operators - Subtraction

```cxy
const func `-`(other: &const Time): Seconds {
    return Seconds{value: _t - other._t}
}

const func `-`(duration: Years): Time {
    return Time(_t - duration.value * 31536000)
}

const func `-`(duration: Months): Time {
    return Time(_t - duration.value * 2592000)
}

const func `-`(duration: Days): Time {
    return Time(_t - duration.value * 86400)
}

const func `-`(duration: Hours): Time {
    return Time(_t - duration.value * 3600)
}

const func `-`(duration: Minutes): Time {
    return Time(_t - duration.value * 60)
}

const func `-`(duration: Seconds): Time {
    return Time(_t - duration.value)
}

const func `-`(duration: Millis): Time {
    return Time(_t - duration.value / 1000)
}
```

### Arithmetic Operators - Addition

```cxy
const func `+`(duration: Years): Time {
    return Time(_t + duration.value * 31536000)
}

const func `+`(duration: Months): Time {
    return Time(_t + duration.value * 2592000)
}

const func `+`(duration: Days): Time {
    return Time(_t + duration.value * 86400)
}

const func `+`(duration: Hours): Time {
    return Time(_t + duration.value * 3600)
}

const func `+`(duration: Minutes): Time {
    return Time(_t + duration.value * 60)
}

const func `+`(duration: Seconds): Time {
    return Time(_t + duration.value)
}

const func `+`(duration: Millis): Time {
    return Time(_t + duration.value / 1000)
}
```

### Comparison Operators

```cxy
const func `<`(other: &const Time): bool {
    return _t < other._t
}

const func `>`(other: &const Time): bool {
    return _t > other._t
}

const func `<=`(other: &const Time): bool {
    return _t <= other._t
}

const func `>=`(other: &const Time): bool {
    return _t >= other._t
}

const func isSameDay(other: &const Time): bool {
    return year() == other.year() && 
           monthValue() == other.monthValue() && 
           dayOfMonth() == other.dayOfMonth()
}
```

### Date Manipulation Methods

```cxy
const func startOfDay(): Time {
    var copy = _tm
    copy.tm_hour = 0
    copy.tm_min = 0
    copy.tm_sec = 0
    return Time(mktime(ptrof copy))
}

const func endOfDay(): Time {
    var copy = _tm
    copy.tm_hour = 23
    copy.tm_min = 59
    copy.tm_sec = 59
    return Time(mktime(ptrof copy))
}

const func startOfMonth(): Time {
    var copy = _tm
    copy.tm_mday = 1
    copy.tm_hour = 0
    copy.tm_min = 0
    copy.tm_sec = 0
    return Time(mktime(ptrof copy))
}

const func endOfMonth(): Time {
    var copy = _tm
    copy.tm_mon++
    copy.tm_mday = 0
    copy.tm_hour = 23
    copy.tm_min = 59
    copy.tm_sec = 59
    return Time(mktime(ptrof copy))
}

const func startOfYear(): Time {
    var copy = _tm
    copy.tm_mon = 0
    copy.tm_mday = 1
    copy.tm_hour = 0
    copy.tm_min = 0
    copy.tm_sec = 0
    return Time(mktime(ptrof copy))
}

const func endOfYear(): Time {
    var copy = _tm
    copy.tm_year++
    copy.tm_mon = 0
    copy.tm_mday = 0
    copy.tm_hour = 23
    copy.tm_min = 59
    copy.tm_sec = 59
    return Time(mktime(ptrof copy))
}
```

### Static Factory Methods

```cxy
@[static]
func fromDate(year: i32, month: i32, day: i32): Time {
    var tm: time_t.tm
    tm.tm_year = year - 1900
    tm.tm_mon = month - 1
    tm.tm_mday = day
    tm.tm_hour = 0
    tm.tm_min = 0
    tm.tm_sec = 0
    tm.tm_isdst = -1
    return Time(mktime(ptrof tm))
}

@[static]
func fromDateTime(year: i32, month: i32, day: i32, hour: i32, minute: i32, second: i32): Time {
    var tm: time_t.tm
    tm.tm_year = year - 1900
    tm.tm_mon = month - 1
    tm.tm_mday = day
    tm.tm_hour = hour
    tm.tm_min = minute
    tm.tm_sec = second
    tm.tm_isdst = -1
    return Time(mktime(ptrof tm))
}

@[static]
func daysInMonth(month: i32, year: i32): i32 {
    if month == 2 {
        var isLeap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)
        return isLeap ? 29 : 28
    }
    if month == 4 || month == 6 || month == 9 || month == 11 {
        return 30
    }
    return 31
}
```

---

## 5. Module-Level Helper Functions

Add after the duration type definitions:

```cxy
pub func now(): Time {
    return Time()
}

pub func today(): Time {
    return Time().startOfDay()
}

pub func yesterday(): Time {
    return today() - Days{value: 1}
}

pub func tomorrow(): Time {
    return today() + Days{value: 1}
}


```

---

## Usage Examples

### Basic Time Operations

```cxy
import { Time, now, today, yesterday, tomorrow } from "stdlib/time.cxy"

// Current time
var current = now()
println("Now: ", current.formatDateTime())

// Today at midnight
var todayStart = today()
println("Today: ", todayStart.formatDate())

// Yesterday and tomorrow
var past = yesterday()
var future = tomorrow()
```

### Duration Arithmetic

```cxy
import { Time, Days, Hours, Minutes, Seconds } from "stdlib/time.cxy"

// Date arithmetic
var startDate = today() - Days{value: 250}
var endDate = today() + Days{value: 30}

// Time arithmetic
var meeting = now() + Hours{value: 2}
var deadline = now() + Minutes{value: 45}

// Calculate time difference
var elapsed = now() - startDate
println("Elapsed: ", elapsed.as[Days]().value, " days")
```

### Duration Conversions

```cxy
import { Days, Hours, Minutes, Seconds, Millis } from "stdlib/time.cxy"

// Convert between units
var oneDay = Days{value: 1}
println(oneDay.as[Hours]().value)      // 24
println(oneDay.as[Minutes]().value)    // 1440
println(oneDay.as[Seconds]().value)    // 86400

// Get raw value as i64
var secondsValue = oneDay.as[i64]()    // Returns the value field directly
println(secondsValue)                   // 86400

// Pretty print durations
var duration = Hours{value: 3}
println(duration)                       // "3 hours"
```

### Component Access

```cxy
import { Time, DayOfWeek, Month } from "stdlib/time.cxy"

var t = now()

println("Year: ", t.year())
println("Month: ", t.month())           // Month enum
println("Month value: ", t.monthValue()) // 1-12
println("Day: ", t.dayOfMonth())
println("Day of week: ", t.dayOfWeek()) // DayOfWeek enum
println("Hour: ", t.hour())
println("Minute: ", t.minute())
println("Second: ", t.second())
```

### Boolean Checks

```cxy
import { Time } from "stdlib/time.cxy"

var t = now()

if t.isWeekend() {
    println("It's the weekend!")
}

if t.isWeekday() {
    println("It's a weekday")
}

if t.isLeapYear() {
    println("This is a leap year")
}

if t.isToday() {
    println("This time is today")
}
```

### Date Manipulation

```cxy
import { Time, today } from "stdlib/time.cxy"

var t = today()

// Get boundaries
var dayStart = t.startOfDay()
var dayEnd = t.endOfDay()
var monthStart = t.startOfMonth()
var monthEnd = t.endOfMonth()
var yearStart = t.startOfYear()
var yearEnd = t.endOfYear()
```

### Comparisons

```cxy
import { Time, today, yesterday } from "stdlib/time.cxy"

var now = today()
var past = yesterday()

if now > past {
    println("Now is after yesterday")
}

if past < now {
    println("Yesterday is before now")
}

if now >= past {
    println("Now is greater than or equal to yesterday")
}

if now.isSameDay(&now) {
    println("Same day")
}
```

### Trading Day Operations

```cxy
import { previousTradingDay, nextTradingDay } from "stdlib/time.cxy"

var prevDay = previousTradingDay()
println("Previous trading day: ", prevDay.formatDate())

var nextDay = nextTradingDay()
println("Next trading day: ", nextDay.formatDate())
```

### Creating Specific Dates

```cxy
import { Time } from "stdlib/time.cxy"

// Create specific date
var birthday = Time.fromDate(1990, 6, 15)
println("Birthday: ", birthday.formatDate())

// Create specific datetime
var appointment = Time.fromDateTime(2024, 12, 25, 14, 30, 0)
println("Appointment: ", appointment.formatDateTime())

// Get days in month
var days = Time.daysInMonth(2, 2024)  // February 2024
println("Days: ", days)  // 29 (leap year)
```

### Enums with String Representation

```cxy
import { Time, DayOfWeek, Month } from "stdlib/time.cxy"

var t = now()

// Day of week
var dow = t.dayOfWeek()
println(dow)  // Prints "Monday", "Tuesday", etc.

// Month
var month = t.month()
println(month)  // Prints "January", "February", etc.

// Use in match
match dow {
    DayOfWeek.Monday => println("Start of work week")
    DayOfWeek.Friday => println("TGIF!")
    DayOfWeek.Saturday, DayOfWeek.Sunday => println("Weekend!")
    ... => println("Midweek")
}
```

---

## Implementation Notes

### Duration Conversions
- All conversions use integer division (truncates fractional values)
- Year = 365 days (approximation, doesn't account for leap years)
- Month = 30 days (approximation)
- Constants: 60 seconds/minute, 60 minutes/hour, 24 hours/day, 1000 millis/second

### Time Arithmetic
- Time arithmetic returns new `Time` objects (immutable)
- Addition/subtraction with durations creates new timestamps
- Time difference (Time - Time) returns `Seconds` duration

### Enumerations
- `DayOfWeek`: 0-6 (Sunday=0, Monday=1, ..., Saturday=6)
- `Month`: 1-12 (January=1, February=2, ..., December=12)
- Both have `@str` attributes for automatic string conversion

### Conversion to i64
- All durations support `.as[i64]()` which returns the raw value
- `Time.as[i64]()` returns the Unix timestamp
- This allows easy integration with APIs expecting numeric timestamps
