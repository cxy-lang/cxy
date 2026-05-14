# DotEnv System Specification for stdlib/dotenv.cxy

## Overview
Simple `.env` file parser with environment variable fallback for `stdlib/dotenv.cxy`.

---

## 1. Core Concept

Parse `.env` files into a simple key-value map. If a key has no value, fallback to reading from system environment.

### .env File Format

```env
# Comments start with #
KEY=value
QUOTED="value with spaces"
SINGLE='single quoted'

# Variable expansion
DATABASE_URL=${DB_HOST}:${DB_PORT}/${DB_NAME}

# Empty values - fallback to environment
PORT=
HOST=

# Explicit empty
EMPTY=""
```

### Parsing Rules

1. **Comments**: Lines starting with `#` are ignored
2. **Empty lines**: Blank lines are ignored
3. **Format**: `KEY=VALUE`
4. **Quotes**: 
   - Double quotes `"..."` allow variable expansion and preserve spaces
   - Single quotes `'...'` are literal (no expansion)
   - Quotes are stripped from final value
5. **Empty values**:
   - `KEY=` (no value) → fallback to `getenv("KEY")`
   - `KEY=""` (explicit empty) → empty string (no fallback)
6. **Variable expansion**:
   - `${VAR}` - replace with value of VAR
   - `${VAR:-default}` - use default if VAR not set or empty

---

## 2. Data Structures

### DotEnv Class

```cxy
pub class DotEnv {
    - vars: HashMap[String, String]
    
    func `init`() {
        vars = HashMap[String, String]()
    }
    
    // Load from string
    func load(str: __string): !void
    
    // Get value (returns None if not found)
    func get(key: string): String?
    
    // Check if key exists
    const func has(key: string): bool
    
    // Get all keys
    const func keys(): Vector[String]
    
    // Get number of variables
    const func size(): u64
    
    // Static factories
    @[static]
    func fromFile(path: string = null): !DotEnv
    
    @[static]
    func fromString(str: __string): !DotEnv
}
```

### Exception Type

```cxy
pub exception DotEnvError(msg: String) => msg.str()
```

---

## 3. API Reference

### Constructor

```cxy
func `init`()
```

Creates a new empty DotEnv instance.

**Example**:
```cxy
var env = DotEnv()
```

---

### load(str: __string): !void

Parses environment variables from a string.

**Parameters**:
- `str`: String content in .env format

**Throws**:
- `DotEnvError`: If content contains invalid syntax

**Example**:
```cxy
var content = "KEY=value\nOTHER=test".s
var env = DotEnv()
env.load(content) catch {
    println("Failed to parse: ", ex!)
}
```

---

### Static: fromString(str: __string): !DotEnv

Parses a string and returns a new DotEnv instance.

**Parameters**:
- `str`: String in .env format

**Returns**: New DotEnv instance with parsed variables

**Throws**:
- `DotEnvError`: If content contains invalid syntax

**Example**:
```cxy
var content = "KEY=value\nOTHER=test".s
var env = DotEnv.fromString(content) catch {
    println("Parse failed")
    return DotEnv()
}
```

---

### get(key: string): String?

Gets the value of a variable.

**Parameters**:
- `key`: Variable name

**Returns**: 
- `Some(value)` if variable exists
- `None` if variable does not exist

**Example**:
```cxy
var port = env.get("PORT")
if !!port {
    println("Port: ", *port)
} else {
    println("PORT not set")
}
```

---

### has(key: string): bool

Checks if a variable exists.

**Parameters**:
- `key`: Variable name

**Returns**: `true` if variable exists, `false` otherwise

**Example**:
```cxy
if env.has("API_KEY") {
    println("API key is configured")
}
```

---

### keys(): Vector[String]

Returns all variable names.

**Returns**: Vector of variable names

**Example**:
```cxy
var allKeys = env.keys()
for key, _ in allKeys {
    println("Key: ", key)
}
```

---

### size(): u64

Returns the number of variables.

**Returns**: Count of variables

**Example**:
```cxy
println("Total variables: ", env.size())
```

---

### Static: fromFile(path: string = null): !DotEnv

Loads a .env file and returns a new DotEnv instance.

**Parameters**:
- `path`: Path to .env file (optional, defaults to smart detection)

**Path Resolution** (if `path` is `null`):
1. If `DOT_ENV` environment variable is set, use that path (highest priority)
2. If `ENV_STAGE` environment variable is set, use `.env.${ENV_STAGE}` if it exists
3. If `__CXY_TEST` is defined (comptime), use `.env.test` if it exists
4. Otherwise, use `.env` (default)

**Returns**: New DotEnv instance with loaded variables

**Throws**:
- `DotEnvError`: If file cannot be read or contains invalid syntax

**Example**:
```cxy
// Smart path detection
var env = DotEnv.fromFile() catch {
    println("Failed to load .env")
    return DotEnv()
}

// Explicit path
var env2 = DotEnv.fromFile(".env.production") catch {
    println("Failed to load .env.production")
    return DotEnv()
}
```

---

## 4. Smart Path Resolution

### Default Path Detection

When `fromFile()` is called without arguments (or with `null`), the path is resolved in this order:

1. **DOT_ENV environment variable** (highest priority)
   ```bash
   export DOT_ENV=/custom/path/.env.custom
   ```
   ```cxy
   var env = DotEnv.fromFile()  // Loads /custom/path/.env.custom
   ```

2. **ENV_STAGE environment variable**
   ```bash
   export ENV_STAGE=production
   ```
   ```cxy
   var env = DotEnv.fromFile()  // Loads .env.production if it exists
   ```

3. **__CXY_TEST comptime flag** (during test execution)
   ```cxy
   #if (defined __CXY_TEST) {
       // Automatically uses .env.test if it exists
       var env = DotEnv.fromFile()
   }
   ```

4. **Default fallback**
   ```cxy
   var env = DotEnv.fromFile()  // Loads .env
   ```

### File Existence Check

For `ENV_STAGE` and `__CXY_TEST`, the file must exist. If it doesn't exist, falls back to the next option in the chain.

---

## 5. Parsing Behavior

### Environment Fallback

When a key has no value, read from system environment:

```env
# .env file
PORT=
HOST=
DATABASE_URL=postgres://localhost/db
```

```cxy
// Assuming PORT=8080 in system environment
var env = DotEnv.fromFile(".env") catch return

var port = env.get("PORT")
// Returns Some("8080") - read from environment

var host = env.get("HOST")
// Returns None - not in environment either

var dbUrl = env.get("DATABASE_URL")
// Returns Some("postgres://localhost/db") - from file
```

### Explicit Empty vs Fallback

```env
EXPLICIT_EMPTY=""
FALLBACK_TO_ENV=
```

- `EXPLICIT_EMPTY` → empty string (no environment lookup)
- `FALLBACK_TO_ENV` → reads from `getenv("FALLBACK_TO_ENV")`

---

## 5. Variable Expansion

### Basic Expansion

Variables can reference other variables or environment variables:

```env
DB_HOST=localhost
DB_PORT=5432
DB_NAME=myapp
DATABASE_URL=postgres://${DB_HOST}:${DB_PORT}/${DB_NAME}
```

Result: `DATABASE_URL=postgres://localhost:5432/myapp`

### Expansion with Defaults

```env
PORT=${PORT:-3000}
HOST=${HOST:-0.0.0.0}
```

If `PORT` environment variable not set, uses `3000`.

### Expansion Order

1. Look up variable in current DotEnv instance
2. If not found, look up in system environment (`getenv`)
3. If not found, use default (if provided) or empty string

---

## 6. Usage Examples

### Basic Loading

```cxy
import { DotEnv } from "stdlib/dotenv.cxy"

func main() {
    // Smart path detection - uses DOT_ENV, ENV_STAGE, or .env
    var env = DotEnv.fromFile() catch {
        println("Could not load .env")
        return
    }
    
    var port = env.get("PORT")
    if !!port {
        println("Server running on port: ", *port)
    }
}
```

---

### Environment-Specific Files

```cxy
import { DotEnv } from "stdlib/dotenv.cxy"

func main() {
    // Set ENV_STAGE=production in environment
    // Automatically loads .env.production if it exists
    var env = DotEnv.fromFile() catch return
    
    var dbUrl = env.get("DATABASE_URL")
    if !!dbUrl {
        println("Database: ", *dbUrl)
    }
}
```

---

### Variable Expansion

```cxy
import { DotEnv } from "stdlib/dotenv.cxy"

func main() {
    var content = """
        DB_HOST=localhost
        DB_PORT=5432
        DB_NAME=myapp
        DATABASE_URL=postgres://${DB_HOST}:${DB_PORT}/${DB_NAME}
    """.s
    
    var env = DotEnv.fromString(content) catch return
    
    var dbUrl = env.get("DATABASE_URL")
    if !!dbUrl {
        println("Database URL: ", *dbUrl)
        // Output: postgres://localhost:5432/myapp
    }
}
```

---

### Testing with .env.test

```cxy
import { DotEnv } from "stdlib/dotenv.cxy"

test "loads test environment" {
    // When __CXY_TEST is defined, automatically uses .env.test
    var env = DotEnv.fromFile() catch {
        println("No .env.test file")
        return
    }
    
    var testDb = env.get("TEST_DATABASE_URL")
    ok!(!!testDb)
}
```

---

### Listing Variables

```cxy
import { DotEnv } from "stdlib/dotenv.cxy"

func main() {
    var env = DotEnv.fromFile(".env") catch return
    
    println("Loaded ", env.size(), " variables:")
    
    var keys = env.keys()
    for key, _ in keys {
        var value = env.get(key)
        if !!value {
            println("  ", key, " = ", *value)
        }
    }
}
```

---

### Error Handling

```cxy
import { DotEnv, DotEnvError } from "stdlib/dotenv.cxy"

func main() {
    var env = DotEnv.fromFile(".env") catch {
        println("Error loading .env: ", ex!)
        return
    }
    
    println("Successfully loaded .env")
}
```

---

## 7. Implementation Notes

### Parsing Algorithm

```
For each line in file:
  1. Trim whitespace
  2. Skip if empty or starts with #
  3. Find first = character
  4. Split into key and value
  5. If value is empty and not quoted:
     - Try getenv(key)
     - If found, use environment value
     - If not found, skip this variable
  6. If value is quoted:
     - Strip quotes
     - If double quotes, expand variables
     - If single quotes, use literal
  7. If value has variables (${VAR}):
     - Expand by looking up in parsed vars or getenv
     - Apply defaults if specified
  8. Store key-value pair
```

### Variable Expansion Implementation

```
1. Find all ${VAR} and ${VAR:-default} patterns using regex or manual parsing
2. For each pattern:
   a. Extract variable name and optional default
   b. Look up in DotEnv instance first
   c. If not found, look up via getenv()
   d. If still not found, use default or empty string
   e. Replace pattern with resolved value
3. Return expanded string
```

### Dependencies

- `stdlib/hashmap.cxy` - for variable storage
- `stdlib/vector.cxy` - for keys() list
- `stdlib/string.cxy` - for String type
- `stdio.h` - for file I/O (fopen, fgets, fclose)
- `stdlib.h` - for getenv()

---

## 8. Example .env Files

### Simple Configuration

```env
# Application settings
APP_NAME=MyApp
VERSION=1.0.0

# Server configuration
PORT=3000
HOST=localhost

# Database
DATABASE_URL=postgres://localhost/myapp
```

### With Environment Fallback

```env
# Use environment variable if set, otherwise use file value
PORT=${PORT:-3000}
HOST=${HOST:-0.0.0.0}

# Empty values fallback to environment
API_KEY=
SECRET_KEY=
```

### With Variable Expansion

```env
# Database components
DB_HOST=localhost
DB_PORT=5432
DB_USER=admin
DB_PASS=secret
DB_NAME=myapp

# Composed URL
DATABASE_URL=postgres://${DB_USER}:${DB_PASS}@${DB_HOST}:${DB_PORT}/${DB_NAME}
```

---

## 9. Best Practices

### Security

1. **Never commit .env files to version control**
   ```bash
   echo ".env" >> .gitignore
   ```

2. **Use .env.example as template**
   ```env
   # .env.example
   PORT=3000
   DATABASE_URL=postgres://localhost/myapp
   API_KEY=your_api_key_here
   ```

3. **Use environment-specific files**
   - `.env` - default/shared values
   - `.env.development` - development overrides
   - `.env.production` - production settings
   - `.env.test` - test environment
   
   ```bash
   # In your deployment
   export ENV_STAGE=production
   
   # Or override completely
   export DOT_ENV=/etc/myapp/.env.production
   ```

### Organization

1. **Group related variables**
   ```env
   # Database
   DB_HOST=localhost
   DB_PORT=5432
   
   # API Keys
   STRIPE_KEY=pk_test_...
   SENDGRID_KEY=SG...
   ```

4. **Use comments for documentation**
   ```env
   # Port number (1-65535)
   PORT=3000
   
   # Environment: development, staging, production
   ENV=development
   ```

5. **Use descriptive names**
   - Good: `DATABASE_CONNECTION_TIMEOUT`
   - Bad: `DB_TO`

---

## 10. Future Enhancements

Potential features for later versions:

1. **Export to environment**
   ```cxy
   env.toEnv()  // Call setenv() for all variables
   ```

2. **Merge multiple files**
   ```cxy
   env.load(".env")
   env.load(".env.local")  // Overrides values
   ```

3. **Type conversion helpers**
   ```cxy
   env.getInt("PORT", 3000)
   env.getBool("DEBUG", false)
   ```

4. **Validation**
   ```cxy
   env.require("API_KEY")  // Throws if not set
   ```

For now, keep it simple: **parse, expand, and fallback to environment**.