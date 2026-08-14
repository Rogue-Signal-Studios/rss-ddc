# CLI Output Presentation

Slice 9 adds a CLI-side presentation layer for interactive readability. The
core `librss-ddc` library is unchanged; all presentation logic lives under
`cli/presentation/`.

## Config file

Read-only user preferences are loaded from:

- `$XDG_CONFIG_HOME/rss-ddc/rss-ddc.conf` when `XDG_CONFIG_HOME` is set
- otherwise `$HOME/.config/rss-ddc/rss-ddc.conf`

A missing config file is normal and produces no warning.

### Schema

```ini
[output]
color = auto
table = auto
unicode = auto
```

Recognized values are `yes`, `no`, and `auto`. Unknown sections and keys are
ignored safely. In `--verbose` mode, ignored entries may produce a warning on
stderr. Malformed recognized values fail with a clear error.

## CLI overrides

Global flags may appear before the command:

```sh
rss-ddc --color=no list
rss-ddc --table=yes probe-quick 1
rss-ddc --unicode=no probe-extended 2
```

Supported options:

- `--color=yes|no|auto`
- `--table=yes|no|auto`
- `--unicode=yes|no|auto`

## Precedence

Effective settings resolve in this order:

1. explicit CLI override
2. environment override (`NO_COLOR`)
3. config file
4. built-in default (`auto` for all three)

`NO_COLOR` disables color at the environment layer. An explicit
`--color=yes` overrides `NO_COLOR`.

## Auto behavior

- `color=auto`: enabled on an interactive terminal (`stdout` TTY and `TERM` not
  `dumb`), disabled when redirected or when `NO_COLOR` applies
- `table=auto`: enabled for supported interactive commands when `stdout` is a
  TTY, disabled when redirected
- `unicode=auto`: enabled when the terminal is interactive and the locale
  appears UTF-8 capable, otherwise disabled

## Plain compatibility

When effective settings are `color=no`, `table=no`, and `unicode=no`, commands
such as `list`, `info`, `get`, `mccs`, `probe-quick`, and `probe-extended`
preserve the existing machine-friendly plain output.

## Table commands

Table rendering is available for:

- `list`
- `probe-quick`
- `probe-extended`

Interactive table mode also prints concise probe summaries using the exact
underlying Slice 8 counters.

## Color semantics

Color communicates meaning rather than decoration:

- stable strict-valid: green
- advertised indicator: cyan
- variable or unusual current > max: yellow
- unadvertised strict-valid: magenta
- malformed, semantic-mismatch, transport-error: red
- protocol-reported and not-attempted repeat: dim/neutral

Output remains understandable without color.

## Unicode

`unicode=yes` may use box-drawing borders. `unicode=no` uses ASCII-only table
borders. Unicode is never required to interpret state.
