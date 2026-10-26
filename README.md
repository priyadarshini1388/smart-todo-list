# Smart To-Do List Web Application

A modern, responsive task management application built with **HTML, CSS, JavaScript, C++17, and SQLite**. It helps users organize daily tasks, manage deadlines, prioritize activities, and track productivity through an interactive dashboard.

## Project Description

The Smart To-Do List is a full-stack web application designed to simplify task management and improve personal productivity. It provides an interactive interface for creating, updating, completing, searching, filtering, and organizing tasks.

The frontend communicates with a C++ HTTP server through REST-style API endpoints. The backend processes requests, validates task data, and stores information in an SQLite database to maintain tasks across application restarts.

## Problem Statement

Managing multiple academic assignments, personal activities, and deadlines can become challenging without an organized tracking system. Traditional to-do lists may lack flexible prioritization, deadline awareness, and progress tracking.

This project addresses these challenges by providing a centralized task management platform with priority indicators, deadline monitoring, task categorization, and productivity statistics.

## Objectives

- Simplify daily task creation and organization.
- Help users prioritize important activities and track deadlines.
- Provide an overview of pending, completed, and overdue tasks.
- Demonstrate full-stack development using web technologies and C++.
- Apply database management, HTTP communication, input validation, and software development principles.

## Technologies Used

| Technology | Purpose |
|---|---|
| HTML5 | Structures the web application interface |
| CSS3 | Provides responsive styling, themes, animations, and layouts |
| JavaScript (ES6+) | Handles user interactions, task rendering, filtering, and API communication |
| C++17 | Implements server-side logic and request processing |
| SQLite3 | Stores tasks persistently in a relational database |
| HTTP | Enables communication between the frontend and backend |
| JSON | Exchanges task data and API responses |
| Git and GitHub | Version control and source code hosting |

**Dependencies:** A C++17-compatible compiler and the SQLite3 development library. The backend uses native socket APIs and a lightweight custom JSON parser; it does not require a third-party HTTP framework.

## Features

### 1. Task Management
- Create tasks with a title, description, priority, category, and optional due date.
- Edit existing tasks.
- Delete individual tasks.
- Mark tasks as completed or pending.
- Clear all completed tasks.
- Undo a recent deletion by recreating the deleted task.

### 2. Smart Quick Add
- Create tasks directly from a quick-entry field.
- Specify priority using shortcuts such as `!high`, `!medium`, and `!low`.
- Assign categories using `#category`.
- Recognize supported date expressions such as `today`, `tomorrow`, `Friday`, `in 3 days`, and `next week`.
- Preview the interpreted task details before adding the task.

**Example:** `Submit assignment tomorrow !high #college`

### 3. Dashboard and Productivity Tracking
- Display total, pending, completed, overdue, and due-today task counts.
- Show an interactive circular completion-progress indicator.
- Provide contextual progress messages based on task status.
- Display task counts for individual categories.

### 4. Search, Filtering, and Sorting
- Search tasks by title, description, or category.
- Filter tasks by active, completed, overdue, or due-today status.
- Filter tasks by priority and category.
- Sort tasks using smart ordering, due date, priority, creation order, or alphabetical order.
- Combine search and filtering criteria.

### 5. User Interface
- Responsive dashboard layout for desktop and mobile screens.
- Light and dark themes.
- Priority-based visual indicators.
- Deadline labels for upcoming and overdue tasks.
- Interactive task cards and an edit form.
- Toast notifications and error messages.
- Keyboard shortcuts for search and closing dialogs.

### 6. Persistent Data Storage
- Automatically initialize the SQLite database and create the tasks table when required.
- Preserve tasks across browser refreshes and backend restarts.
- Use prepared SQL statements with bound parameters for database operations.
- Maintain task creation and completion timestamps.
- Use database indexes to support common task queries.

## System Architecture

The application follows a frontend–backend–database architecture.

```text
+-----------------------------------+
|             Frontend              |
|       HTML + CSS + JavaScript     |
|                                   |
| Dashboard, Forms, Search, Filters |
+----------------+------------------+
                 |
                 | HTTP Requests
                 | JSON Responses
                 v
+-----------------------------------+
|           C++ Backend             |
|              C++17                |
|                                   |
| HTTP Server | Routing | Validation|
| JSON Parsing | Task Operations    |
+----------------+------------------+
                 |
                 | SQLite API
                 v
+-----------------------------------+
|            SQLite3                |
|                                   |
| Tasks, Priorities, Due Dates,     |
| Categories and Timestamps         |
+-----------------------------------+
```

### Application Workflow

1. The user performs an action through the web interface.
2. JavaScript sends an HTTP request to the C++ backend.
3. The backend parses the request and validates the supplied data.
4. SQLite executes the required database operation.
5. The backend returns a JSON response and an appropriate HTTP status code.
6. JavaScript updates the task list and dashboard without a full page reload.

## Project Structure

The current implementation uses the following files:

```text
smart-todo-list/
├── index.html
├── todo_server.cpp
├── todo.db                 # Generated at runtime
├── README.md
├── .gitignore
└── screenshots/
    └── dashboard.png       # Optional screenshot
```

| File | Description |
|---|---|
| `index.html` | Contains the HTML structure, embedded CSS, and JavaScript logic |
| `todo_server.cpp` | Implements the HTTP server, JSON parsing, API routing, validation, and SQLite operations |
| `todo.db` | SQLite database created when the server starts |
| `README.md` | Project documentation, setup, and usage instructions |
| `.gitignore` | Excludes generated files and local development artifacts |
| `screenshots/` | Stores screenshots of the application |

The database and screenshot files are optional repository artifacts; the database should be generated locally rather than committed.

## Database Design

The application creates a `tasks` table automatically.

| Column | Data type | Description |
|---|---|---|
| `id` | INTEGER | Auto-incrementing primary key |
| `title` | TEXT | Required task title |
| `description` | TEXT | Optional task description |
| `priority` | TEXT | `low`, `medium`, or `high` |
| `category` | TEXT | Task category; defaults to `General` |
| `due_date` | TEXT | Optional date in `YYYY-MM-DD` format |
| `completed` | INTEGER | Completion status: 0 or 1 |
| `created_at` | TEXT | Task creation timestamp |
| `completed_at` | TEXT | Completion timestamp, when applicable |

### Database Implementation Details

- `CREATE TABLE IF NOT EXISTS` initializes the schema.
- Prepared statements and bound parameters are used for task queries and updates.
- Indexes are created on `due_date` and `completed`.
- SQLite write-ahead logging (WAL) is enabled.
- A database busy timeout is configured to help handle temporary database contention.

## REST API Documentation

The backend exposes the following endpoints on the local server, using port `8080` by default.

| Method | Endpoint | Description |
|---|---|---|
| GET | `/` | Serves the web application |
| GET | `/api/health` | Checks server availability |
| GET | `/api/tasks` | Retrieves tasks with optional filters |
| GET | `/api/tasks/{id}` | Retrieves a task by ID |
| POST | `/api/tasks` | Creates a new task |
| PUT | `/api/tasks/{id}` | Updates an existing task |
| PATCH | `/api/tasks/{id}` | Partially updates a task |
| PATCH | `/api/tasks/{id}/toggle` | Toggles task completion |
| DELETE | `/api/tasks/{id}` | Deletes an individual task |
| DELETE | `/api/tasks?completed=true` | Deletes all completed tasks |
| GET | `/api/stats` | Retrieves productivity statistics |

### Supported Query Parameters

The task-list endpoint supports:

- `q` — searches titles, descriptions, and categories.
- `status` — filters by `active`, `completed`, `overdue`, or `today`.
- `priority` — filters by priority.
- `category` — filters by category.
- `sort` — supports `smart`, `due`, `priority`, `newest`, `oldest`, and `title`.

**Example request:**

```http
GET /api/tasks?status=active&priority=high&sort=due
```

This retrieves active high-priority tasks sorted by due date.

### Example JSON Request

```json
{
  "title": "Complete C++ assignment",
  "description": "Practice arrays and functions",
  "priority": "high",
  "category": "College",
  "due_date": "2026-10-30",
  "completed": false
}
```

### Example Health Check Response

```json
{
  "status": "ok"
}
```

The backend returns appropriate HTTP status codes for successful requests, invalid input, missing tasks, and server errors.

## Installation and Execution

### Prerequisites

- Git (optional, for cloning the repository)
- A C++17-compatible compiler
- SQLite3 development headers and library
- A terminal or command prompt
- A modern web browser

### 1. Clone the Repository

Replace `YOUR-USERNAME` with your GitHub username.

```bash
git clone https://github.com/YOUR-USERNAME/smart-todo-list.git
cd smart-todo-list
```

Alternatively, download the repository as a ZIP file and extract it.

### 2. Install Dependencies

#### Windows using MSYS2 UCRT64

Install MSYS2 and open its **UCRT64 terminal**. Install the compiler and SQLite development package:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sqlite3
```

Ensure the UCRT64 `bin` directory is available in your terminal's PATH.

#### Linux (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install g++ libsqlite3-dev
```

#### macOS

Install Xcode Command Line Tools if needed, then install SQLite using Homebrew:

```bash
xcode-select --install
brew install sqlite
```

Ensure the compiler can locate the SQLite headers and library.

### 3. Compile the C++ Backend

Run the appropriate command from the directory containing `todo_server.cpp` and `index.html`.

**Windows — MSYS2 UCRT64:**

```bash
g++ -std=c++17 -O2 todo_server.cpp -lsqlite3 -lws2_32 -o todo_server.exe
```

**Linux:**

```bash
g++ -std=c++17 -O2 -pthread todo_server.cpp -lsqlite3 -o todo_server
```

**macOS:**

```bash
g++ -std=c++17 -O2 -pthread todo_server.cpp -lsqlite3 -o todo_server
```

If SQLite is installed in a nonstandard directory on macOS, configure the appropriate include and library search paths.

### 4. Start the Application

**Windows:**

```bash
./todo_server.exe
```

**Linux/macOS:**

```bash
./todo_server
```

By default, the server uses port `8080`, creates `todo.db` in the current working directory, and looks for `index.html` in the current working directory.

Open the following URL in your browser:

**http://localhost:8080**

Keep the terminal open while using the application. Stop the server with `Ctrl+C`.

### Optional: Custom Port and Database Path

The backend accepts command-line arguments in this order:

```text
todo_server [port] [database_file] [index_html_path]
```

For example:

```bash
./todo_server 8081 todo.db index.html
```

This starts the server on port `8081`.

## Testing and Verification

Use the following checklist to verify the application:

- [ ] The homepage loads successfully.
- [ ] The health endpoint returns a successful response.
- [ ] New tasks can be created with valid data.
- [ ] Existing tasks can be edited and deleted.
- [ ] Completion status updates correctly.
- [ ] Search, filtering, and sorting return the expected results.
- [ ] Due-today and overdue counts are accurate.
- [ ] Dashboard statistics update after task operations.
- [ ] Tasks remain available after restarting the backend.
- [ ] Invalid task titles and priority values are rejected.
- [ ] The interface works on mobile and desktop screen sizes.
- [ ] Light and dark themes work correctly.

### Useful API Checks

Check server health in a browser:

```text
http://localhost:8080/api/health
```

Retrieve all tasks:

```text
http://localhost:8080/api/tasks
```

Retrieve statistics:

```text
http://localhost:8080/api/stats
```

To create a task, use an API client such as `curl`:

```bash
curl -X POST http://localhost:8080/api/tasks \
  -H "Content-Type: application/json" \
  -d '{"title":"Test task","priority":"high","category":"Testing"}'
```

On Windows, run the command in a terminal that supports `curl`.

## Error Handling and Validation

The backend validates task fields before writing to the database.

- Task titles cannot be empty and are limited to 200 characters.
- Descriptions are limited to 2,000 characters.
- Categories are limited to 40 characters.
- Priority must be `low`, `medium`, or `high`.
- Due dates must follow the `YYYY-MM-DD` format.
- Invalid JSON, unsupported methods, and nonexistent task IDs produce error responses.

The frontend also displays server-connection errors and provides feedback for task operations.

## Security and Limitations

This project is intended for local development and educational use.

- The server binds to the loopback interface by default, making it accessible locally rather than directly to other devices on the network.
- The current implementation does not provide user authentication or per-user task isolation.
- Cross-origin requests are permitted by the current server configuration; this should be restricted before deployment.
- The custom HTTP and JSON implementations are lightweight educational components, not replacements for thoroughly tested production-grade libraries.
- Date validation checks the input format and basic month/day ranges; it does not fully validate every calendar date.
- The application should be reviewed and hardened before exposure to untrusted networks.

Do not store passwords, API keys, or other sensitive information in the repository.

## Future Enhancements

- Add secure user authentication and user-specific task lists.
- Implement recurring tasks and reminder notifications.
- Add calendar integration and task export.
- Introduce automated unit and integration tests.
- Improve accessibility and keyboard navigation.
- Replace custom HTTP and JSON handling with established libraries.
- Add deployment configuration and continuous integration.

## What I Learned

Through this project, I explored:

- Building responsive web interfaces with HTML, CSS, and JavaScript.
- Connecting a browser-based frontend to a C++ backend using HTTP and JSON.
- Designing REST-style API endpoints and implementing CRUD operations.
- Integrating SQLite with C++ for persistent data storage.
- Using prepared SQL statements, database indexes, and transactions-related database concepts.
- Implementing input validation, filtering, sorting, and task statistics.
- Organizing source code, debugging, and documenting a software project.
- Using Git and GitHub for version control and project maintenance.

## Contribution

This project is developed as an individual learning project. Contributions and suggestions for improvements are welcome.

1. Fork the repository.
2. Create a feature branch.
3. Commit your changes with meaningful messages.
4. Test your changes.
5. Submit a pull request describing the improvements.

## Author

PRIYADARSHINI S

GitHub: priyadarshini1388

---

*Built as a learning project to explore full-stack development with web technologies, C++, and SQLite.*
