/**
 * <!--------------------------------------------------------------------------
 * This file is part of libSBMLSim.  Please visit
 * http://fun.bio.keio.ac.jp/software/libsbmlsim/ for more
 * information about libSBMLSim and its latest version.
 *
 * Copyright (C) 2011-2017 by the Keio University, Yokohama, Japan
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.  A copy of the license agreement is provided
 * in the file named "LICENSE.txt" included with this software distribution.
 * ---------------------------------------------------------------------- -->*/
#include "libsbmlsim/libsbmlsim.h"

void minus_func(ASTNode_t *node){
  unsigned int i;
  ASTNode_t *next_node, *zero_node;
  for(i=0; i<ASTNode_getNumChildren(node); i++){
    next_node = ASTNode_getChild(node, i);
    if(ASTNode_getNumChildren(node) == 1 && ASTNode_getType(node) == AST_MINUS){
      zero_node = ASTNode_create();
      ASTNode_setType(zero_node, AST_REAL);
      ASTNode_setReal(zero_node, 0);
      ASTNode_replaceChild(node, 0, zero_node);
      ASTNode_addChild(node, next_node);
    }else{
      minus_func(next_node);
    }
  }
  return;
}

void alter_tree_structure(Model_t *m, ASTNode_t **node_p, ASTNode_t *parent, int child_order, copied_AST *cp_AST){
  ASTNode_t *zero_node;
  ASTNode_t *compartment_node;
  ASTNode_t *node, *next_node;
  ASTNode_t *pc_eq, *pc_cd, *times_node, *and_node, *not_node, *divide_node;
  unsigned int i, j;
  int p;
  ASTNode_t *arg_node_list[MAX_ARG_NUM];
  unsigned int arg_node_num;
  FunctionDefinition_t *fd;
  ASTNode_t *fd_arg;
  ASTNode_t *fd_body;
  Species_t *sp;

  node = *node_p;
  /* Check whether this node is '-' and only have one child. */
  if(ASTNode_getNumChildren(node) == 1 && ASTNode_getType(node) == AST_MINUS){
    next_node = ASTNode_getChild(node, 0);
    zero_node = ASTNode_create();
    ASTNode_setType(zero_node, AST_REAL);
    ASTNode_setReal(zero_node, 0);
    ASTNode_replaceChild(node, 0, zero_node);
    ASTNode_addChild(*node_p, next_node);
    /* checked no leak just by code reading 2017-11-17 by funa */
  }
  /* Have to treat with special situations that there is no child under operator */
  /* (but MathML allows it) */
  if (ASTNode_getNumChildren(node) == 0) {
    if( ASTNode_getType(node) == AST_PLUS ) { /* change to 0.0 */
      ASTNode_setType(node, AST_REAL);
      ASTNode_setReal(node, 0.0);
    } else if ( ASTNode_getType(node) == AST_TIMES ) { /* change to 1.0 */
      ASTNode_setType(node, AST_REAL);
      ASTNode_setReal(node, 1.0);
    } else if ( ASTNode_getType(node) == AST_LOGICAL_AND ) {
      ASTNode_setType(node, AST_INTEGER);
      ASTNode_setReal(node, true);
    } else if ( ASTNode_getType(node) == AST_LOGICAL_OR  || ASTNode_getType(node) == AST_LOGICAL_XOR ){
      ASTNode_setType(node, AST_INTEGER);
      ASTNode_setReal(node, false);
    }
    /* checked no leak just by code reading 2017-11-17 by funa */
  }
  /* Have to treat with special situations that it has only 1 child under operator */
  /* (but MathML allows it) */
  if (ASTNode_getNumChildren(node) == 1) {
    if( ASTNode_getType(node) == AST_PLUS  || ASTNode_getType(node) == AST_TIMES ||
        ASTNode_getType(node) == AST_LOGICAL_AND ||
        ASTNode_getType(node) == AST_LOGICAL_OR ||
        ASTNode_getType(node) == AST_LOGICAL_XOR ) { /* change to its child value (convert as 'child + 0' */
      /* change myself as '+' */
      ASTNode_setType(node, AST_PLUS);
      /* creat 0 node */
      zero_node = ASTNode_create();
      ASTNode_setType(zero_node, AST_REAL);
      ASTNode_setReal(zero_node, 0);
      /* add 0 to right node */
      ASTNode_addChild(node, zero_node);
    }
    /* checked no leak just by code reading 2017-11-17 by funa */
  }

  /* Do the same thing for children (recursive) */
  for(i = 0; i < ASTNode_getNumChildren(node); i++) {
    next_node = ASTNode_getChild(node, i);
    /* TRACE(("down to %d th child from\n", i)); */
    /* print_node_type(node); */
    alter_tree_structure(m, &next_node, *node_p, i, cp_AST);
  }

  /* If node is Name (Species, Parameter, etc.) */
  /* !!! CAUTION !!! this part should not be before the recursive call!!! */
  if(ASTNode_getType(node) == AST_NAME) {
    for(i = 0; i < Model_getNumSpecies(m); i++) {
      sp = (Species_t*)ListOf_get(Model_getListOfSpecies(m), i);
      if(strcmp(Species_getId(sp), ASTNode_getName(node)) == 0){
        if(!Species_getHasOnlySubstanceUnits(sp) && Species_isSetInitialAmount(sp) && Compartment_getSpatialDimensions(Model_getCompartmentById(m, Species_getCompartment(sp))) != 0){/* use val/comp in calculation */
          divide_node = ASTNode_createWithType(AST_DIVIDE);
          compartment_node = ASTNode_createWithType(AST_NAME);
          ASTNode_setName(compartment_node, Compartment_getId(Model_getCompartmentById(m, Species_getCompartment(sp))));
          ASTNode_addChild(divide_node, node);
          ASTNode_addChild(divide_node, compartment_node);
          if(parent != NULL){
            ASTNode_replaceChild(parent, child_order, divide_node);
          }else{
            *node_p = divide_node;
          }
          node = *node_p;
          break;
        }else if(Species_getHasOnlySubstanceUnits(sp) && Species_isSetInitialConcentration(sp) && Compartment_getSpatialDimensions(Model_getCompartmentById(m, Species_getCompartment(sp))) != 0){/*  use val*comp in calculation */
          times_node = ASTNode_createWithType(AST_TIMES);
          compartment_node = ASTNode_createWithType(AST_NAME);
          ASTNode_setName(compartment_node, Compartment_getId(Model_getCompartmentById(m, Species_getCompartment(sp))));
          ASTNode_addChild(times_node, node);
          ASTNode_addChild(times_node, compartment_node);
          if(parent != NULL){
            ASTNode_replaceChild(parent, child_order, times_node);
          }else{
            *node_p = times_node;
          }
          node = *node_p;
          break;
        }
      }
    }
  }
  /* If node is Function (don't call this function for children. */
  if(ASTNode_getType(node) == AST_FUNCTION){
    arg_node_num = ASTNode_getNumChildren(node);
    for(i=0; i<arg_node_num; i++){
      arg_node_list[i] = ASTNode_getChild(node, i);
    }
    for(i=0; i<Model_getNumFunctionDefinitions(m); i++){
      fd = (FunctionDefinition_t*)ListOf_get(Model_getListOfFunctionDefinitions(m), i);
      fd_body = (ASTNode_t*)FunctionDefinition_getBody(fd);
      if(strcmp(FunctionDefinition_getId(fd), ASTNode_getName(node)) == 0) {
        fd_body = ASTNode_deepCopy(fd_body);
        /* cp_AST->ast[cp_AST->num_of_copied_AST++] = fd_body; */
        for(j=0; j<FunctionDefinition_getNumArguments(fd); j++){
          fd_arg = (ASTNode_t*)FunctionDefinition_getArgument(fd, j);
          ASTNode_replaceArgument(fd_body, (char*)ASTNode_getName(fd_arg), arg_node_list[j]);
        }
        /* Support nested functions */
        /* Confirmed by funa & takizawa 2013/03/16 */
        /* We can not uncomment this part because it causes SEGV on free_mySBML_object() */
        /* But this part is required if we want to call function from function (nested functions) */
          /* for(i=0; i<ASTNode_getNumChildren(fd_body); i++){ */
            /* next_node = ASTNode_getChild(fd_body, i); */
            /* TRACE(("down to %d th child from\n", i)); */
            /* print_node_type(node); */
            /* alter_tree_structure(m, &next_node, fd_body, i, cp_AST); */
          /* } */
         /* Uncomment end */
        minus_func(fd_body);
        check_AST(fd_body, NULL);
        if(parent != NULL){
          ASTNode_replaceChild(parent, child_order, fd_body);
        }else{
          *node_p = fd_body;
        }
        add_ast_memory_node(node, __FILE__, __LINE__);
        node = *node_p;
        break;
      }
    }
  }
  /* If node is Piecewise */
  if(ASTNode_getType(node) == AST_FUNCTION_PIECEWISE){
    if(ASTNode_getNumChildren(node) % 2 == 0) {
      /* creat 0 node */
      zero_node = ASTNode_create();
      ASTNode_setType(zero_node, AST_REAL);
      ASTNode_setReal(zero_node, 0);
      /* add 0 to right node */
      ASTNode_addChild(node, zero_node);
    }
    ASTNode_setName(node, NULL);
    ASTNode_setType(node, AST_PLUS);
    times_node = ASTNode_createWithType(AST_TIMES);
    pc_eq = ASTNode_getRightChild(node);
    ASTNode_addChild(times_node, pc_eq);
    if(ASTNode_getNumChildren(node) > 3){
      and_node = ASTNode_createWithType(AST_LOGICAL_AND);
      ASTNode_addChild(times_node, and_node);
      for(p=(int)ASTNode_getNumChildren(node)-2; p >= 1; p = p-2){
        pc_cd = ASTNode_getChild(node, p);
        not_node = ASTNode_createWithType(AST_LOGICAL_NOT);
        ASTNode_addChild(not_node, pc_cd);
        ASTNode_addChild(and_node, not_node);
      }
      ASTNode_reduceToBinary(and_node);
    }else{
      pc_cd = ASTNode_getChild(node, 1);
      not_node = ASTNode_createWithType(AST_LOGICAL_NOT);
      ASTNode_addChild(not_node, pc_cd);
      ASTNode_addChild(times_node, not_node);
    }
    ASTNode_replaceChild(node, ASTNode_getNumChildren(node)-1, times_node);
    for(p=(int)ASTNode_getNumChildren(node)-2; p >= 1; p = p-2){
      times_node = ASTNode_createWithType(AST_TIMES);
      pc_eq = ASTNode_getChild(node, p-1);
      pc_cd = ASTNode_getChild(node, p);
      ASTNode_addChild(times_node, pc_eq);
      ASTNode_addChild(times_node, ASTNode_deepCopy(pc_cd));
      ASTNode_removeChild(node, p);
      ASTNode_replaceChild(node ,p-1, times_node);
    }
    ASTNode_reduceToBinary(node);
  }
  /* print_node_type(node); */
  /* TRACE(("is proccessed\n")); */
  return;
}
