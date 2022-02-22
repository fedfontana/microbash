## Membri del gruppo:

- Fabio Fontana s4891185
- Federico Fontana s4835118
- Roberto Castellotti s4801634

## Valgrind

`valgrind --leak-check=full ./microbash.out`

## Note

Nella funzione `exec` abbiamo deciso di introdurre un `goto` per evitare di scrivere codice duplicato per le free degli stessi puntatori:
La parte della funzione interessata prima era:

```c
        else if(command_no > 0)
        {
            for(int i = 0; i < command_no; i++)
            {
                if(strcmp(line[i][0], "cd") == 0)
                {
                    fprintf(stderr, "\'cd\' must be used alone (too many commands).\n");
                    exit_status = 1;

                    for(int i = 0; i < command_no; i++)
                        free(line[i]);
                    free(line);

                    free(input_redirect);
                    free(output_redirect);
                    return 1;
                }
            }
            exit_status = exec(line, command_no, input_redirect, output_redirect);
        }
    }
    else
        exit_status = 1;

    for(int i = 0; i < command_no; i++)
        free(line[i]);
    free(line);

    free(input_redirect);
    free(output_redirect);
    return exit_status;
}
```

mentre con il `goto` è:

```c
        else if(command_no > 0)
        {
            for(int i = 0; i < command_no; i++)
            {
                if(strcmp(line[i][0], "cd") == 0)
                {
                    fprintf(stderr, "\'cd\' must be used alone (too many commands).\n");
                    exit_status = 1;
                    goto FREE;
                }
            }
            exit_status = exec(line, command_no, input_redirect, output_redirect);
        }
    }
    else
        exit_status = 1;

FREE:
    for(int i = 0; i < command_no; i++)
        free(line[i]);
    free(line);

    free(input_redirect);
    free(output_redirect);
    return exit_status;
}
```
