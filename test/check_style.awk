# ---------------------------------------------------------------------------
# check_style.awk - Rule 0 and project coding style enforcement
#
# Checks every file passed on the command line for:
#   1. Line length <= 80 characters
#   2. No // comments
#   3. No tab characters or CR (\r) line endings
#   4. No trailing whitespace
#   5. Allman braces: no cuddled '{' or '}' on control-flow lines
#   6. No forbidden keywords (goto, continue)
# ---------------------------------------------------------------------------

BEGIN {
    errors = 0
}

{
    line = $0
    sub(/\r$/, "", line)

    if ($0 ~ /\r$/) {
        printf("%s:%d: CRLF line ending\n", FILENAME, FNR)
        errors++
    }

    if (length(line) > 80) {
        printf("%s:%d: line length %d > 80\n", FILENAME, FNR, length(line))
        errors++
    }

    if (line ~ /\t/) {
        printf("%s:%d: tab character found\n", FILENAME, FNR)
        errors++
    }

    if (line ~ /[ \t]+$/) {
        printf("%s:%d: trailing whitespace\n", FILENAME, FNR)
        errors++
    }

    if (line ~ /\/\//) {
        printf("%s:%d: forbidden // comment\n", FILENAME, FNR)
        errors++
    }

    if (line !~ /^[ \t]*(\/\*|\*)/) {
        if (line ~ /\)[ \t]*\{/ || line ~ /\belse[ \t]*\{/ || line ~ /\}[ \t]*else\b/) {
            printf("%s:%d: cuddled brace violates Allman style\n", FILENAME, FNR)
            errors++
        }

        if (line ~ /(^|[^a-zA-Z0-9_])(goto|continue)([^a-zA-Z0-9_]|$)/) {
            printf("%s:%d: forbidden keyword (goto/continue)\n", FILENAME, FNR)
            errors++
        }
    }
}

END {
    if (errors > 0) {
        printf("Style check FAILED: %d error(s)\n", errors)
        exit 1
    }
}
