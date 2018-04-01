; ModuleID = 'test_interprocedural_call_sites.bc'
source_filename = "test_interprocedural_call_sites.c"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@temp = global i32 0, align 4

; Function Attrs: noinline nounwind uwtable
define i32 @foo() #0 {
entry:
  %retval = alloca i32, align 4
  %0 = load i32, i32* @temp, align 4
  %cmp = icmp eq i32 %0, 5
  br i1 %cmp, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  store i32 0, i32* %retval, align 4
  br label %return

if.end:                                           ; preds = %entry
  %1 = load i32, i32* @temp, align 4
  store i32 %1, i32* %retval, align 4
  br label %return

return:                                           ; preds = %if.end, %if.then
  %2 = load i32, i32* %retval, align 4
  ret i32 %2
}

; Function Attrs: noinline nounwind uwtable
define i32 @main() #0 {
entry:
  %retval = alloca i32, align 4
  %x = alloca i32, align 4
  store i32 0, i32* %retval, align 4
  %0 = load i32, i32* @temp, align 4
  %tobool = icmp ne i32 %0, 0
  br i1 %tobool, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  store i32 5, i32* @temp, align 4
  br label %if.end

if.else:                                          ; preds = %entry
  store i32 1, i32* @temp, align 4
  br label %if.end

if.end:                                           ; preds = %if.else, %if.then
  %call = call i32 @foo()
  store i32 %call, i32* %x, align 4
  %1 = load i32, i32* %x, align 4
  %cmp = icmp eq i32 %1, 0
  br i1 %cmp, label %if.then1, label %if.else2

if.then1:                                         ; preds = %if.end
  store i32 1, i32* @temp, align 4
  br label %if.end3

if.else2:                                         ; preds = %if.end
  store i32 2, i32* @temp, align 4
  br label %if.end3

if.end3:                                          ; preds = %if.else2, %if.then1
  ret i32 0
}

attributes #0 = { noinline nounwind uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.ident = !{!0}

!0 = !{!"clang version 4.0.1 (tags/RELEASE_401/final)"}
