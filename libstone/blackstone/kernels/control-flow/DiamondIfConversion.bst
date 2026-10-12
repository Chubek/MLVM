// Speculated operations must be safe, pure/total under the appropriate host model;
// conversion preserves poison/undef, effects, data dependencies and merge typing.
Kernel DiamondIfConversion;
concerns HIR::ControlFlow::Region;
concerns HIR::ControlFlow::Operation;
requires Optimization::ControlFlow::IfConversionSafe;
PredicateDiamond :: Binary(Term!resolves, Term!resolves) -> Binary
where IfConversionSafe { $1 `diamond` $2 ~> $1 `predicated_select` $2; }
