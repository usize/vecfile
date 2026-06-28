```
                                                          
mmmmm                            ,...,,    ,,       mmmmm 
MM                             .d' ""db  `7MM          MM 
MM                             dM`         MM          MM 
MM `7M'   `MF'.gP"Ya   ,p6"bo mMMmm`7MM    MM  .gP"Ya  MM 
MM   VA   ,V ,M'   Yb 6M'  OO  MM    MM    MM ,M'   Yb MM 
MM    VA ,V  8M"""""" 8M       MM    MM    MM 8M"""""" MM 
MM     VVV   YM.    , YM.    , MM    MM    MM YM.    , MM 
MM      W     `Mbmmd'  YMbmd'.JMML..JMML..JMML.`Mbmmd' MM 
MM                                                     MM 
MMmmm                                               mmmMM
```

### What

A single binary with an internal embedding model, inference engine and sqlite support.
This is an APE binary--see [cosmopolitan](https://github.com/jart/cosmopolitan)--so it's
highly portable.

### Why

I'm tired of elaborate memory systems. A single binary and a db file should be enough.

I can pack it around with a skill for use by any LLM.

### Usage
```
vecfile <command> [options]
```

### Namespaces

A namespace is an isolated search index with its own chunk settings.

```
vecfile ns create  --db PATH --name NS [--chunk-size N] [--chunk-overlap N]
vecfile ns list    --db PATH
vecfile ns info    --db PATH --name NS
vecfile ns delete  --db PATH --name NS
```

### Adding content

```
vecfile add --db PATH --ns NS "some text"                   # literal
echo "text" | vecfile add --db PATH --ns NS -               # stdin
vecfile add --db PATH --ns NS --file doc.txt                # file
vecfile add --db PATH --ns NS ~/notes/*.md                  # wildcard
```

Content is SHA256-hashed on ingest. Re-running the same add is a no-op
for unchanged content.

Options: `--tag NAME` (label for stdin/literal), `--meta JSON`,
`--on-dup skip|replace`.

### Querying

```
vecfile query --db PATH --ns NS "search terms"              # hybrid (default)
vecfile query --db PATH --ns NS --semantic-only "concept"   # vector only
vecfile query --db PATH --ns NS --lexical-only "keyword"    # BM25 only
vecfile query --db PATH --ns NS --chunks "search terms"     # chunk-level
```

Options: `--limit N`, `--pool N`, `--rrf-k N`.

### Retrieving content

```
vecfile get --db PATH --id N                                # file by id
vecfile get --db PATH --tag T                               # file by tag
vecfile get --db PATH --chunk N                             # single chunk
vecfile get --db PATH --chunk N -C 2                        # chunk + context
```

`-C N` returns N chunks before and after the match, like `grep -C`.

### Deleting

```
vecfile delete --db PATH --ns NS --id N                     # by file id
vecfile delete --db PATH --ns NS --path P                   # by path
vecfile delete --db PATH --ns NS --all                      # wipe namespace
```

### Info

```
vecfile model                                               # bundled model info
vecfile --version                                           # version string
```

