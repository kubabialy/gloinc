#ifndef GLOIN_BASE
#define GLOIN_BASE

include "mlir/IR/OpBase.td"

def Gloin_Dialect : Dialect {
  let name = "gloin";
  let cppNamespace = "::gloin";
  let description = [{
    The Gloin language dialect.
  }];
  
  let useDefaultTypePrinterParser = 1;
}

#endif // GLOIN_BASE
