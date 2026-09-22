# Commit Message Convention

This project follows a structured commit message convention to keep the history readable, searchable, and easy for both humans and AI agents to follow.

## Format

All commit messages must use the following format:

```txt
TYPE(scope): summary
```

TYPE is mandatory and MUST be uppercase.

### Valid / Invalid Examples

**Valid:**
- `FEAT(player): add sheet info overlay toggle`
- `FIX(player): sync practice cursor after seeking`
- `DOCS(readme): add Google auth setup guide`

**Invalid:**
- `feat(player): add sheet info overlay toggle`
- `Fix(player): sync practice cursor after seeking`
- `docs(readme): add Google auth setup guide`

### General Rules

1. **TYPE** is mandatory and MUST be uppercase. Lowercase or mixed-case types (e.g., `feat`, `Fix`) are strictly prohibited.
2. **scope** must be lowercase.
3. **summary** should be in English.
4. **summary** should be concise and use the imperative mood (e.g., "add", "fix", "change", not "added", "fixes", "changing").
5. Do not end the summary with a period.
6. Prefer one logical change per commit.

---

## Allowed Types

Allowed types are:

- **FEAT**: A new user-facing or system feature
  - *Example:* `FEAT(player): add sheet info overlay toggle`
- **FIX**: A bug fix
  - *Example:* `FIX(player): sync practice cursor after seeking`
- **REFACT**: A code refactoring without intended behavior changes
  - *Example:* `REFACT(player): split falling notes from player mode state`
- **CHORE**: Maintenance, dependency updates, cleanup, and helper scripts
  - *Example:* `CHORE(env): update local example variables`
- **DOCS**: Documentation-only changes
  - *Example:* `DOCS(identity): document Google OAuth setup`
- **STYLE**: Formatting or visual/styling-only changes
  - *Example:* `STYLE(ui): polish account card spacing`
- **TEST**: Adding or modifying tests
  - *Example:* `TEST(player): add chord hit detection tests`
- **PERF**: A performance optimization
  - *Example:* `PERF(storage): dedupe canonical sheet payloads`
- **SEC**: Security improvements or vulnerability patches
  - *Example:* `SEC(payments): verify Lemon webhook signatures`
- **OPS**: Infrastructure, deployment, health checks, observability, or runbooks
  - *Example:* `OPS(infra): add service health checks`
- **MIGRATE**: Database migrations or database schema/sqlc changes
  - *Example:* `MIGRATE(db): add username cooldown timestamp`
- **REMOVE**: Removing deprecated features, code, or configuration
  - *Example:* `REMOVE(identity): remove Kratos recovery route`

*Note: Lowercase or mixed-case types are not allowed.*

---

## Recommended Scopes

Use the most specific scope that describes the main changed area. If a change spans multiple areas, use the primary one. Use `frontend` or `backend` only when no narrower scope fits.

- `identity` - Authentication, sessions, OAuth, verification
- `frontend` - Frontend application-wide shell or layouts
- `backend` - Backend service-wide framework or router
- `player` - Sheet playback engine, view mode, audio
- `sheets` - Sheet metadata, CRUD API, upload logic
- `payments` - Purchase flows, checkouts, Lemon Squeezy integration
- `entitlements` - Sheet access rights, ownership verification
- `profiles` - User display name, avatar, account details
- `admin` - Moderation, reporting, auditing panels
- `directus` - Directus admin dashboard configuration
- `storage` - Blob storage, object stores, bucket policies
- `search` - Algolia indices, search queries, relevancy tuning
- `analytics` - Tracking scripts, clickstream events, telemetry
- `jobs` - Background workers, queues, cron jobs
- `ui` - Common styling, Design System components
- `docs` - Project documentation or developer guides
- `infra` - Docker Compose, environment configuration, system tasks
- `db` - Database schema, migrations, sqlc queries

### Scope Examples

- `FEAT(profiles): add editable account page`
- `FIX(player): prevent keyboard clipping on mobile`
- `MIGRATE(db): create user_profiles table`
- `OPS(infra): add production compose health checks`
- `DOCS(directus): add troubleshooting guide`

---

## Summary Style

Summaries must be descriptive and action-oriented. Avoid vague descriptions.

- **Good:**
  - `FIX(player): prevent keyboard clipping on mobile`
  - `FEAT(payments): add Lemon checkout provider`
  - `DOCS(readme): add local Google auth setup`
- **Bad:**
  - `FIX(player): fix bug`
  - `CHORE: update`
  - `FEAT: changes`
  - `REFACT(frontend): improve stuff`

---

## Commit Body

For larger or more complex commits, use an optional body to explain the details.

```txt
FEAT(player): add falling notes performance layout

- move Falling Notes from mode to Performance option
- add responsive falling notes and keyboard split layout
- auto-fit keyboard scale when Falling Notes is enabled
- keep Note Runner independent from mode
```

- Separate the summary from the body with a blank line.
- Use bullet points for multi-part changes.
- Explain *why* the change was made if the reason is not obvious.
- Keep the body concise.

---

## Breaking Changes

To indicate a breaking change, add a `!` after the scope, or add a `BREAKING CHANGE:` footer at the bottom of the commit body.

### Syntax 1: Using `!` after scope

```txt
FEAT(identity)!: replace Kratos with Google JWT auth
```

### Syntax 2: Using a footer

```txt
FEAT(identity): replace Kratos with Google JWT auth

BREAKING CHANGE: Users must sign in with Google. Existing Kratos password and recovery flows are removed.
```

### When to use Breaking Changes

- Removed API endpoints or pages
- Removed auth providers or login flows
- Incompatible API request/response structures
- Database schema changes requiring manual data migrations or downtime
- Behavioral changes that older clients/services cannot handle

---

## Multi-Area Changes

To keep the git history clean, prefer splitting changes across separate commits:

1. `MIGRATE(db): add user profile username cooldown`
2. `FEAT(profiles): enforce username cooldown in profile API`
3. `FEAT(frontend): show username cooldown on account page`

*Note: If the change is small and tightly coupled, one single commit is acceptable using the primary scope.*

---

## Project-Specific Reference Examples

### Identity
- `FEAT(identity): add Google-only JWT session flow`
- `FIX(identity): validate OAuth state before token exchange`
- `SEC(identity): reject weak JWT secrets in production`
- `REMOVE(identity): drop Kratos browser flow wiring`
- `DOCS(identity): document Google OAuth setup`

### Player
- `FEAT(player): add falling notes density setting`
- `FIX(player): highlight all practice chord guide keys`
- `FIX(player): sync practice cursor after seeking`
- `FIX(player): prevent keyboard clipping on mobile`
- `REFACT(player): move Falling Notes into Performance option`

### Profiles
- `FEAT(profiles): add editable account settings page`
- `FIX(profiles): reject avatar updates from profile patch`
- `FEAT(profiles): sync avatar from Google picture`
- `MIGRATE(db): add username cooldown timestamp`

### Payments
- `FEAT(payments): add Lemon checkout provider`
- `FIX(payments): make webhook processing idempotent`
- `SEC(payments): verify Lemon webhook signatures`
- `REFACT(payments): move provider logic into payment service`

### Storage
- `MIGRATE(db): add sheet blob storage metadata`
- `FEAT(storage): store canonical compressed sheet blobs`
- `PERF(storage): dedupe canonical sheet payloads by content hash`
- `FIX(sheets): preserve chord keys during runtime normalization`

### Docs/Ops
- `DOCS(readme): add local Google auth setup guide`
- `DOCS(directus): add empty collections troubleshooting`
- `OPS(infra): add production health checks`
- `CHORE(env): update example JWT auth variables`

---

## For AI Agents

AI agents cooperating in this workspace must follow these rules:

1. **Adherence:** Always suggest commit messages that follow this convention.
2. **Precision:** Never suggest vague summaries (e.g. `update`, `bug fix`, `refactor`).
3. **Database Changes:** Always use the `MIGRATE(db)` type for any SQL migration files.
4. **Security/Ops:** Use `SEC(...)` for security-sensitive additions, and `OPS(infra)` for deployment, health check, log sanitation, or metrics changes.
5. **Breaking Changes:** Explicitly mark any breaking changes with the `!` scope modifier or the `BREAKING CHANGE:` footer.
6. **Casing Rules:**
   - Always write `TYPE` in uppercase.
   - Never suggest lowercase or mixed-case commit types.
   - If the user provides a lowercase type, normalize it to uppercase.

---

## Optional Validation

If commit linting is added later, enforce:
- `type-case`: uppercase
- `type-enum`: `FEAT`, `FIX`, `REFACT`, `CHORE`, `DOCS`, `STYLE`, `TEST`, `PERF`, `SEC`, `OPS`, `MIGRATE`, `REMOVE`
- `scope-case`: lowercase
- `subject-case`: sentence-case is not required; keep summaries concise and imperative

*Note: Do not add commitlint config in this task unless explicitly requested. Only document the rules.*
