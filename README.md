# filet

A fucking fucking fast file fucker (a so called fufufafifu)

## Usage

`filet`. Done.

Optionally give it a directory to open like this `filet <dir>`.

Also you can use the following snippet to automatically switch to the directory you quit in.

```bash
f() {
    filet "$@"
    cd "$(< /tmp/filet_dir)"
}
```

If you want to open files, you need to set `FILET_OPENER` to something like `xdg-open`.

## Why?

```
             -     =    .--._
       - - ~_=  =~_- = - `.  `-.
     ==~_ = =_  ~ -   =  .-'    `.
   --=~_ - ~  == - =   .'      _..:._
  ---=~ _~  = =-  =   `.  .--.'      `.
 --=_-=- ~= _ - =  -  _.'  `.      .--.:
   -=_~ -- = =  ~-  .'      :     :    :
    -=-_ ~=  = - _-`--.     :  .--:    D
      -=~ _=  =  -~_=  `;  .'.:   ,`---'@
    --=_= = ~-   -=   .'  .'  `._ `-.__.'
   --== ~_ - =  =-  .'  .'     _.`---'
  --=~_= = - = ~  .'--''   .   `-..__.--.
 jgs--==~ _= - ~-=  =-~_-   `-..___(  ===;
 --==~_==- =__ ~-=  - -    .'       `---'
```
