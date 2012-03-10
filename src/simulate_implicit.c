#include "sbml/common/common.h"
#include "sbml/SBMLReader.h"
#include "sbml/SBMLTypes.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <float.h>
#include "header.h"

//coefficient matrix for implicit numerical integration
int c_i[7][9] = {{1, -1, 0, 0, 0, -1, 0, 0, 0}, //AM1 & AM2 (Backword-Eular) : orcer = 0
  {2, -2, 0, 0, 0, -1, -1, 0, 0}, //AM2 (Crank-Nicolson) : order = 1
  {12, -12, 0, 0, 0, -5, -8, 1, 0}, //AM3 : order = 2
  {24, -24, 0, 0, 0, -9, -19, 5, -1}, //AM4 : order = 3
  {3, -4, 1, 0, 0, -2, 0, 0, 0}, //BD2 : order = 4
  {11, -18, 9, -2, 0, -6, 0, 0, 0}, //BD3 : order = 5
  {25, -48, 36, -16, 3, -12, 0, 0, 0}}; //BD4 : order = 6

//x1 = x(t+1), x2 = x(t) x3 = x(t-1), x4 = x(t-2), x5 = x(t-3)
//k1 = dx(t+1)/d(t+1), k2 = dx(t)/dt, k3 = dx(t-1)/d(t-1), k4 = dx(t-2)/d(t-2)
double calc_implicit_formula(int order, double x1, double x2, double x3, double x4, double x5, double k1, double k2, double k3, double k4, double dt){
  return c_i[order][0]*x1 + c_i[order][1]*x2 + c_i[order][2]*x3 + c_i[order][3]*x4 + c_i[order][4]*x5 + dt*(c_i[order][5]*k1 + c_i[order][6]*k2 + c_i[order][7]*k3 + c_i[order][8]*k4);
}

void seed_set_imp(){
  srand((unsigned)time(NULL));
}

myResult* simulate_implicit(Model_t *m, myResult *result, mySpecies *sp[], myParameter *param[], myCompartment *comp[], myReaction *re[], myRule *rule[], myEvent *event[], myInitialAssignment *initAssign[], myAlgebraicEquations *algEq, timeVariantAssignments *timeVarAssign, double sim_time, double dt, int print_interval, double *time, int order, int use_lazy_method, int print_amount, allocated_memory *mem){
  seed_set_imp();
  int i, j, cycle;
  double reverse_time;
  int end_cycle = get_end_cycle(sim_time, dt);
  int num_of_species = Model_getNumSpecies(m);
  int num_of_parameters = Model_getNumParameters(m);
  int num_of_compartments = Model_getNumCompartments(m);
  int num_of_reactions = Model_getNumReactions(m);
  int num_of_rules = Model_getNumRules(m);
  int num_of_events = Model_getNumEvents(m);
  int num_of_initialAssignments = Model_getNumInitialAssignments(m);

  int num_of_all_var_species = 0; //量が変化する全ての分子の数
  int num_of_all_var_parameters = 0; //量が変化する全てのパラメータの数
  int num_of_all_var_compartments = 0; //量が変化する全てのコンパートメントの数
  int num_of_all_var_species_reference = 0;
  int num_of_var_species = 0; //assignment, algebraic rule以外で量が変化する分子の数
  int num_of_var_parameters = 0; //assignment, algebraic rule以外で量が変化するパラメータの数
  int num_of_var_compartments = 0; //assignment, algebraic rule以外で量が変化するコンパートメントの数
  int num_of_var_species_reference = 0;

  check_num(num_of_species, num_of_parameters, num_of_compartments, num_of_reactions, &num_of_all_var_species, &num_of_all_var_parameters, &num_of_all_var_compartments, &num_of_all_var_species_reference, &num_of_var_species, &num_of_var_parameters, &num_of_var_compartments, &num_of_var_species_reference, sp, param, comp, re);

  mySpecies *all_var_sp[num_of_all_var_species]; //all variable species
  myParameter *all_var_param[num_of_all_var_parameters]; //all variable parameters
  myCompartment *all_var_comp[num_of_all_var_compartments]; //all variable compartments
  mySpeciesReference *all_var_spr[num_of_all_var_species_reference];
  mySpecies *var_sp[num_of_var_species]; //variable species (species which change their value with assignment and algebraic rule are excluded)
  myParameter *var_param[num_of_var_parameters]; //variable parameters (parameters which change their value with assignment and algebraic rule are excluded)
  myCompartment *var_comp[num_of_var_compartments]; //variable compartments (parameters which change their value with assignment and algebraic rule are excluded)
  mySpeciesReference *var_spr[num_of_var_species_reference];

  create_calc_object_list(num_of_species, num_of_parameters, num_of_compartments, num_of_reactions, num_of_all_var_species, num_of_all_var_parameters, num_of_all_var_compartments, num_of_all_var_species_reference, num_of_var_species, num_of_var_parameters, num_of_var_compartments, num_of_var_species_reference, all_var_sp, all_var_param, all_var_comp, all_var_spr, var_sp, var_param, var_comp, var_spr, sp, param, comp, re);

  double **jacobian;
  jacobian = (double**)malloc(sizeof(double*)*(num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference));
  for(i=0; i<num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference; i++){
    jacobian[i] = (double*)malloc(sizeof(double)*(num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference));
  }

  double b[num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference];
  double pre_b[num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference];
  int is_convergence = 0;
  int p[num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference]; //for pivot selection
  int flag;
  double delta = 1.0e-8; //
  double tolerance = 1.0e-4; //error tolerance of neuton method
  int loop;
  double delta_value[num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference];
  int error;

  double k_next; //speculated k value : k(t+1)
  double k_t[num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference]; //k(t)

  double **coefficient_matrix = NULL;
  double *constant_vector = NULL;
  int *alg_pivot = NULL;
  if(algEq != NULL){
    coefficient_matrix = (double**)malloc(sizeof(double*)*(algEq->num_of_algebraic_variables));
    for(i=0; i<algEq->num_of_algebraic_variables; i++){
      coefficient_matrix[i] = (double*)malloc(sizeof(double)*(algEq->num_of_algebraic_variables));
    }
    constant_vector = (double*)malloc(sizeof(double)*(algEq->num_of_algebraic_variables));
    alg_pivot = (int*)malloc(sizeof(int)*(algEq->num_of_algebraic_variables));
  }

  double reactants_numerator, products_numerator;
  double min_value;

  FILE *fp1, *fp2, *fp3;
  fp1 = fopen("./simulation_results/species_result.dat", "w");
  fp2 = fopen("./simulation_results/parameter_result.dat", "w");
  fp3 = fopen("./simulation_results/compartment_result.dat", "w");
  /*   fp1 = fopen("./out.csv", "w"); */
  /*   fprintf(fp1, "time"); */
  /*   for(i=0; i<num_of_species; i++){ */
  /*     fprintf(fp1, ",%s", Species_getId(sp[i]->origin)); */
  /*   } */
  /*   for(i=0; i<num_of_parameters; i++){ */
  /*     fprintf(fp1, ",%s", Parameter_getId(param[i]->origin)); */
  /*   } */
  /*   for(i=0; i<num_of_compartments; i++){ */
  /*     fprintf(fp1, ",%s", Compartment_getId(comp[i]->origin)); */
  /*   } */
  /*   fprintf(fp1, "\n"); */

  printf("Simulation Starts!\n");
  cycle = 0;

  //initialize delay_val
  initialize_delay_val(sp, num_of_species, param, num_of_parameters, comp, num_of_compartments, re, num_of_reactions, sim_time, dt, 0);

  //calc temp value by assignment
  for(i=0; i<num_of_all_var_species; i++){
    if(all_var_sp[i]->depending_rule != NULL && all_var_sp[i]->depending_rule->is_assignment){
      all_var_sp[i]->temp_value = calc(all_var_sp[i]->depending_rule->eq, dt, cycle, &reverse_time, 0);
    }
  }
  for(i=0; i<num_of_all_var_parameters; i++){
    if(all_var_param[i]->depending_rule != NULL && all_var_param[i]->depending_rule->is_assignment){
      all_var_param[i]->temp_value = calc(all_var_param[i]->depending_rule->eq, dt, cycle, &reverse_time, 0);
    }
  }
  for(i=0; i<num_of_all_var_compartments; i++){
    if(all_var_comp[i]->depending_rule != NULL && all_var_comp[i]->depending_rule->is_assignment){
      all_var_comp[i]->temp_value = calc(all_var_comp[i]->depending_rule->eq, dt, cycle, &reverse_time, 0);
    }
  }
  for(i=0; i<num_of_all_var_species_reference; i++){
    if(all_var_spr[i]->depending_rule != NULL && all_var_spr[i]->depending_rule->is_assignment){
      all_var_spr[i]->temp_value = calc(all_var_spr[i]->depending_rule->eq, dt, cycle, &reverse_time, 0);
    }
  }
  //forwarding value
  forwarding_value(all_var_sp, num_of_all_var_species, all_var_param, num_of_all_var_parameters, all_var_comp, num_of_all_var_compartments, all_var_spr, num_of_all_var_species_reference);

  //initialize delay_val
  initialize_delay_val(sp, num_of_species, param, num_of_parameters, comp, num_of_compartments, re, num_of_reactions, sim_time, dt, 0);

  //calc InitialAssignmet
  calc_initial_assignment(initAssign, num_of_initialAssignments, dt, cycle, &reverse_time);

  //initialize delay_val
  initialize_delay_val(sp, num_of_species, param, num_of_parameters, comp, num_of_compartments, re, num_of_reactions, sim_time, dt, 0);

  //rewriting for explicit delay
  double *init_val;
  for(i=0; i<num_of_initialAssignments; i++){
    for(j=0; j<initAssign[i]->eq->math_length; j++){
      if(initAssign[i]->eq->number[j] == time){
        printf("time is replaced with reverse time\n");
        initAssign[i]->eq->number[j] = &reverse_time;
      }else if(initAssign[i]->eq->number[j] != NULL){
        init_val = (double*)malloc(sizeof(double));
        *init_val = *initAssign[i]->eq->number[j];
        mem->memory[mem->num_of_allocated_memory++] = init_val;
        initAssign[i]->eq->number[j] = init_val;
      }
    }
  }
  for(i=0; i<timeVarAssign->num_of_time_variant_assignments; i++){
    for(j=0; j<timeVarAssign->eq[i]->math_length; j++){
      if(timeVarAssign->eq[i]->number[j] == time){
        printf("time is replaced with reverse time\n");
        timeVarAssign->eq[i]->number[j] = &reverse_time;
      }else if(timeVarAssign->eq[i]->number[j] != NULL){
        init_val = (double*)malloc(sizeof(double));
        *init_val = *timeVarAssign->eq[i]->number[j];
        mem->memory[mem->num_of_allocated_memory++] = init_val;
        timeVarAssign->eq[i]->number[j] = init_val;
      }
    }
  }

  //initialize delay_val
  initialize_delay_val(sp, num_of_species, param, num_of_parameters, comp, num_of_compartments, re, num_of_reactions, sim_time, dt, 0);

  //calc temp value by assignment
  for(i=0; i<num_of_all_var_species; i++){
    if(all_var_sp[i]->depending_rule != NULL && all_var_sp[i]->depending_rule->is_assignment){
      all_var_sp[i]->temp_value = calc(all_var_sp[i]->depending_rule->eq, dt, cycle, &reverse_time, 0);
    }
  }
  for(i=0; i<num_of_all_var_parameters; i++){
    if(all_var_param[i]->depending_rule != NULL && all_var_param[i]->depending_rule->is_assignment){
      all_var_param[i]->temp_value = calc(all_var_param[i]->depending_rule->eq, dt, cycle, &reverse_time, 0);
    }
  }
  for(i=0; i<num_of_all_var_compartments; i++){
    if(all_var_comp[i]->depending_rule != NULL && all_var_comp[i]->depending_rule->is_assignment){
      all_var_comp[i]->temp_value = calc(all_var_comp[i]->depending_rule->eq, dt, cycle, &reverse_time, 0);
    }
  }
  for(i=0; i<num_of_all_var_species_reference; i++){
    if(all_var_spr[i]->depending_rule != NULL && all_var_spr[i]->depending_rule->is_assignment){
      all_var_spr[i]->temp_value = calc(all_var_spr[i]->depending_rule->eq, dt, cycle, &reverse_time, 0);
    }
  }
  //forwarding value
  forwarding_value(all_var_sp, num_of_all_var_species, all_var_param, num_of_all_var_parameters, all_var_comp, num_of_all_var_compartments, all_var_spr, num_of_all_var_species_reference);

  //initialize delay_val
  initialize_delay_val(sp, num_of_species, param, num_of_parameters, comp, num_of_compartments, re, num_of_reactions, sim_time, dt, 0);

  //calc temp value algebraic by algebraic
  if(algEq != NULL){
    if(algEq->num_of_algebraic_variables > 1){
      //initialize pivot
      for(i=0; i<algEq->num_of_algebraic_variables; i++){
        alg_pivot[i] = i;
      }
      for(i=0; i<algEq->num_of_algebraic_variables; i++){
        for(j=0; j<algEq->num_of_algebraic_variables; j++){
          coefficient_matrix[i][j] = calc(algEq->coefficient_matrix[i][j], dt, cycle, &reverse_time, 0);
        }
      }
      for(i=0; i<algEq->num_of_algebraic_variables; i++){
        constant_vector[i] = -calc(algEq->constant_vector[i], dt, cycle, &reverse_time, 0);
      }
      //LU decompostion
      error = lu_decomposition(coefficient_matrix, alg_pivot, algEq->num_of_algebraic_variables);
      if(error == 0){//failure in LU decomposition
        return NULL;
      }
      //forward substitution & backward substitution
      lu_solve(coefficient_matrix, alg_pivot, algEq->num_of_algebraic_variables, constant_vector);
      for(i=0; i<algEq->num_of_alg_target_sp; i++){
        algEq->alg_target_species[i]->target_species->temp_value = constant_vector[algEq->alg_target_species[i]->order];
      }    
      for(i=0; i<algEq->num_of_alg_target_param; i++){
        algEq->alg_target_parameter[i]->target_parameter->temp_value = constant_vector[algEq->alg_target_parameter[i]->order];
      }    
      for(i=0; i<algEq->num_of_alg_target_comp; i++){
        algEq->alg_target_compartment[i]->target_compartment->temp_value = constant_vector[algEq->alg_target_compartment[i]->order];
      }    
    }else{
      if(algEq->target_species != NULL){
        algEq->target_species->temp_value = -calc(algEq->constant, dt, cycle, &reverse_time, 0)/calc(algEq->coefficient, dt, cycle, &reverse_time, 0);
      }
      if(algEq->target_parameter != NULL){
        algEq->target_parameter->temp_value = -calc(algEq->constant, dt, cycle, &reverse_time, 0)/calc(algEq->coefficient, dt, cycle, &reverse_time, 0);
      }
      if(algEq->target_compartment != NULL){
        algEq->target_compartment->temp_value = -calc(algEq->constant, dt, cycle, &reverse_time, 0)/calc(algEq->coefficient, dt, cycle, &reverse_time, 0);
      }
    }
    //forwarding value
    forwarding_value(all_var_sp, num_of_all_var_species, all_var_param, num_of_all_var_parameters, all_var_comp, num_of_all_var_compartments, all_var_spr, num_of_all_var_species_reference);
  }

  //determin species initial amount (for independent species)
  for(i=0; i<num_of_species; i++){
    if(sp[i]->is_amount){
      sp[i]->initial_amount = sp[i]->value;
    }else{
      sp[i]->initial_amount = sp[i]->value*sp[i]->locating_compartment->value;
    }
  }

  //initialize delay_val
  initialize_delay_val(sp, num_of_species, param, num_of_parameters, comp, num_of_compartments, re, num_of_reactions, sim_time, dt, 1);

  //cycle start
  for(cycle=0; cycle<=end_cycle; cycle++){
    //calculate unreversible fast reaction
    for(i=0; i<num_of_reactions; i++){
      if(re[i]->is_fast && !re[i]->is_reversible){
        if(calc(re[i]->eq, dt, cycle, &reverse_time, 0) > 0){
          min_value = DBL_MAX;
          for(j=0; j<re[i]->num_of_reactants; j++){
            if(min_value > re[i]->reactants[j]->mySp->value/calc(re[i]->reactants[j]->eq, dt, cycle, &reverse_time, 0)){
              min_value = re[i]->reactants[j]->mySp->value/calc(re[i]->reactants[j]->eq, dt, cycle, &reverse_time, 0);
            }
          }
          for(j=0; j<re[i]->num_of_products; j++){
            if(!Species_getBoundaryCondition(re[i]->products[j]->mySp->origin)){
              re[i]->products[j]->mySp->value += calc(re[i]->products[j]->eq, dt, cycle, &reverse_time, 0)*min_value;
              re[i]->products[j]->mySp->temp_value = re[i]->products[j]->mySp->value;
            }
          }
          for(j=0; j<re[i]->num_of_reactants; j++){
            if(!Species_getBoundaryCondition(re[i]->reactants[j]->mySp->origin)){
              re[i]->reactants[j]->mySp->value -= calc(re[i]->reactants[j]->eq, dt, cycle, &reverse_time, 0)*min_value;
              re[i]->reactants[j]->mySp->temp_value = re[i]->reactants[j]->mySp->value;
            }
          }
        }
      }
    }
    //calculate reversible fast reactioin
    for(i=0; i<num_of_reactions; i++){
      if(re[i]->is_fast && re[i]->is_reversible){
        if(!(Species_getBoundaryCondition(re[i]->products[0]->mySp->origin) 
              && Species_getBoundaryCondition(re[i]->reactants[0]->mySp->origin))){
          products_numerator = calc(re[i]->products_equili_numerator, dt, cycle, &reverse_time, 0);
          reactants_numerator = calc(re[i]->reactants_equili_numerator, dt, cycle, &reverse_time, 0);
          if(products_numerator > 0 || reactants_numerator > 0){
            if(Species_getBoundaryCondition(re[i]->products[0]->mySp->origin)){
              re[i]->reactants[0]->mySp->value = (reactants_numerator/products_numerator)*re[i]->products[0]->mySp->value;
              re[i]->reactants[0]->mySp->temp_value = re[i]->reactants[0]->mySp->value;
            }else if(Species_getBoundaryCondition(re[i]->reactants[0]->mySp->origin)){
              re[i]->products[0]->mySp->value = (products_numerator/reactants_numerator)*re[i]->reactants[0]->mySp->value;
              re[i]->products[0]->mySp->temp_value = re[i]->products[0]->mySp->value;	    
            }else{
              re[i]->products[0]->mySp->value = (products_numerator/(products_numerator+reactants_numerator))*(re[i]->products[0]->mySp->temp_value+re[i]->reactants[0]->mySp->temp_value);
              re[i]->reactants[0]->mySp->value = (reactants_numerator/(products_numerator+reactants_numerator))*(re[i]->products[0]->mySp->temp_value+re[i]->reactants[0]->mySp->temp_value);
              re[i]->products[0]->mySp->temp_value = re[i]->products[0]->mySp->value;
              re[i]->reactants[0]->mySp->temp_value = re[i]->reactants[0]->mySp->value;
            }
          }
        }
      }
    }

    //event
    calc_event(event, num_of_events, dt, *time, cycle, &reverse_time);    

    //substitute delay val
    substitute_delay_val(sp, num_of_species, param, num_of_parameters, comp, num_of_compartments, re, num_of_reactions, cycle);

    //progress
    if(cycle%(int)(end_cycle/10) == 0){
      printf("%3d %%\n", (int)(100*((double)cycle/(double)end_cycle)));
      printf("\x1b[1A");
      printf("\x1b[5D");
    }
    //print result
    if(cycle%print_interval == 0){
      fprintf(fp1, "%lf", *time);
      for(i=0; i<num_of_species; i++){
        if(!(Species_getConstant(sp[i]->origin) && Species_getBoundaryCondition(sp[i]->origin))){
          if(print_amount){
            if(sp[i]->is_concentration){
              fprintf(fp1, " %.16g", sp[i]->value*sp[i]->locating_compartment->value);
            }else{
              fprintf(fp1, " %.16g", sp[i]->value);
            }
          }else{
            if(sp[i]->is_amount){
              fprintf(fp1, " %.16g", sp[i]->value/sp[i]->locating_compartment->value);
            }else{
              fprintf(fp1, " %.16g", sp[i]->value);
            }
          }
        }
      }
      fprintf(fp1, "\n");
      fprintf(fp2, "%lf", *time);
      for(i=0; i<num_of_parameters; i++){
        if(!Parameter_getConstant(param[i]->origin)){
          fprintf(fp2, " %.16g", param[i]->value);
        }
      }
      fprintf(fp2, "\n");
      fprintf(fp3, "%lf", *time);
      for(i=0; i<num_of_compartments; i++){
        if(!Compartment_getConstant(comp[i]->origin)){
          fprintf(fp3, " %.16g", comp[i]->value);
        }
      }
      fprintf(fp3, "\n");
    }

    /*     //print result */
    /*     if(cycle%print_interval == 0){ */
    /*       fprintf(fp1, "%g", *time); */
    /*       for(i=0; i<num_of_species; i++){ */
    /* 	if(print_amount){ */
    /* 	  if(sp[i]->is_concentration){ */
    /* 	    fprintf(fp1, " ,%.16g", sp[i]->value*sp[i]->locating_compartment->value); */
    /* 	  }else{ */
    /* 	    fprintf(fp1, " ,%.16g", sp[i]->value); */
    /* 	  } */
    /* 	}else{ */
    /* 	  if(sp[i]->is_amount){ */
    /* 	    fprintf(fp1, " ,%.16g", sp[i]->value/sp[i]->locating_compartment->value); */
    /* 	  }else{ */
    /* 	    fprintf(fp1, " ,%.16g", sp[i]->value); */
    /* 	  } */
    /* 	} */
    /*       } */
    /*       for(i=0; i<num_of_parameters; i++){ */
    /*     	fprintf(fp1, " ,%.16g", param[i]->value); */
    /*       } */
    /*       for(i=0; i<num_of_compartments; i++){ */
    /*     	fprintf(fp1, " ,%.16g", comp[i]->value); */
    /*       } */
    /*       fprintf(fp1, "\n"); */
    /*     } */

    //time increse
    *time = (cycle+1)*dt;

    //implicit method
    //define init value by eular start
    calc_k(all_var_sp, num_of_all_var_species, all_var_param, num_of_all_var_parameters, all_var_comp, num_of_all_var_compartments, all_var_spr, num_of_all_var_species_reference, re, num_of_reactions, rule, num_of_rules, cycle, dt, &reverse_time, 0, 1);

    //preserve k(t) value
    for(i=0; i<num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference; i++){
      if(i < num_of_var_species){
        k_t[i] = var_sp[i]->k[0];
      }else if(i < num_of_var_species+num_of_var_parameters){
        k_t[i] = var_param[i-num_of_var_species]->k[0];
      }else if(i < num_of_var_species+num_of_var_parameters+num_of_var_compartments){
        k_t[i] = var_comp[i-num_of_var_species-num_of_var_parameters]->k[0];
      }else{
        k_t[i] = var_spr[i-num_of_var_species-num_of_var_parameters-num_of_var_compartments]->k[0];
      }
    }

    calc_temp_value(all_var_sp, num_of_all_var_species, all_var_param, num_of_all_var_parameters, all_var_comp, num_of_all_var_compartments, all_var_spr, num_of_all_var_species_reference, cycle, dt, 0);
    //define init value by eular end

    //newton method
    if(use_lazy_method){
      is_convergence = 0;
      for(i=0; i<num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference; i++){
        pre_b[i] = 0;
      }
    }
    flag = 1;
    while(flag){
      //calc b
      calc_k(var_sp, num_of_var_species, var_param, num_of_var_parameters, var_comp, num_of_var_compartments, var_spr, num_of_var_species_reference, re, num_of_reactions, rule, num_of_rules, cycle, dt, &reverse_time, 0, 0);
      for(i=0; i<num_of_var_species; i++){
        k_next = var_sp[i]->k[0];
        b[i] = calc_implicit_formula(order, var_sp[i]->temp_value, var_sp[i]->value, var_sp[i]->prev_val[0], var_sp[i]->prev_val[1], var_sp[i]->prev_val[2], k_next, k_t[i], var_sp[i]->prev_k[0], var_sp[i]->prev_k[1], dt);
      }
      for(i=0; i<num_of_var_parameters; i++){
        b[num_of_var_species+i] = calc_implicit_formula(order, var_param[i]->temp_value, var_param[i]->value, var_param[i]->prev_val[0], var_param[i]->prev_val[1], var_param[i]->prev_val[2], var_param[i]->k[0], k_t[num_of_var_species+i], var_param[i]->prev_k[0], var_param[i]->prev_k[1], dt);
      }
      for(i=0; i<num_of_var_compartments; i++){
        b[num_of_var_species+num_of_var_parameters+i] = calc_implicit_formula(order, var_comp[i]->temp_value, var_comp[i]->value, var_comp[i]->prev_val[0], var_comp[i]->prev_val[1], var_comp[i]->prev_val[2], var_comp[i]->k[0], k_t[num_of_var_species+num_of_var_parameters+i], var_comp[i]->prev_k[0], var_comp[i]->prev_k[1], dt);
      }
      for(i=0; i<num_of_var_species_reference; i++){
        b[num_of_var_species+num_of_var_parameters+num_of_var_compartments+i] = calc_implicit_formula(order, var_spr[i]->temp_value, var_spr[i]->value, var_spr[i]->prev_val[0], var_spr[i]->prev_val[1], var_spr[i]->prev_val[2], var_spr[i]->k[0], k_t[num_of_var_species+num_of_var_parameters+num_of_var_compartments+i], var_spr[i]->prev_k[0], var_spr[i]->prev_k[1], dt);
      }

      if(!use_lazy_method || !is_convergence){
        //calc jacobian by numerical differentiation
        for(loop=0; loop<num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference; loop++){
          if(loop < num_of_var_species){
            var_sp[loop]->temp_value += delta;
          }else if(loop < num_of_var_species+num_of_var_parameters){
            var_param[loop-num_of_var_species]->temp_value += delta;
          }else if(loop < num_of_var_species+num_of_var_parameters+num_of_var_compartments){
            var_comp[loop-num_of_var_species-num_of_var_parameters]->temp_value += delta;
          }else{
            var_spr[loop-num_of_var_species-num_of_var_parameters-num_of_var_compartments]->temp_value += delta;
          }
          calc_k(var_sp, num_of_var_species, var_param, num_of_var_parameters, var_comp, num_of_var_compartments, var_spr, num_of_var_species_reference, re, num_of_reactions, rule, num_of_rules, cycle, dt, &reverse_time, 0, 0);
          for(i=0; i<num_of_var_species; i++){
            k_next = var_sp[i]->k[0];
            delta_value[i] = calc_implicit_formula(order, var_sp[i]->temp_value, var_sp[i]->value, var_sp[i]->prev_val[0], var_sp[i]->prev_val[1], var_sp[i]->prev_val[2], k_next, k_t[i], var_sp[i]->prev_k[0], var_sp[i]->prev_k[1], dt);
            //numerical differentiation
            jacobian[i][loop] = (delta_value[i]-b[i])/delta;
          }
          for(i=0; i<num_of_var_parameters; i++){
            delta_value[num_of_var_species+i] = calc_implicit_formula(order, var_param[i]->temp_value, var_param[i]->value, var_param[i]->prev_val[0], var_param[i]->prev_val[1], var_param[i]->prev_val[2], var_param[i]->k[0], k_t[num_of_var_species+i], var_param[i]->prev_k[0], var_param[i]->prev_k[1], dt);
            //numerical differentiation
            jacobian[num_of_var_species+i][loop] = (delta_value[num_of_var_species+i]-b[num_of_var_species+i])/delta;
          }
          for(i=0; i<num_of_var_compartments; i++){
            delta_value[num_of_var_species+num_of_var_parameters+i] = calc_implicit_formula(order, var_comp[i]->temp_value, var_comp[i]->value, var_comp[i]->prev_val[0], var_comp[i]->prev_val[1], var_comp[i]->prev_val[2], var_comp[i]->k[0], k_t[num_of_var_species+num_of_var_parameters+i], var_comp[i]->prev_k[0], var_comp[i]->prev_k[1], dt);
            //numerical differentiation
            jacobian[num_of_var_species+num_of_var_parameters+i][loop] = (delta_value[num_of_var_species+num_of_var_parameters+i]-b[num_of_var_species+num_of_var_parameters+i])/delta;
          }
          for(i=0; i<num_of_var_species_reference; i++){
            delta_value[num_of_var_species+num_of_var_parameters+num_of_var_compartments+i] = calc_implicit_formula(order, var_spr[i]->temp_value, var_spr[i]->value, var_spr[i]->prev_val[0], var_spr[i]->prev_val[1], var_spr[i]->prev_val[2], var_spr[i]->k[0], k_t[num_of_var_species+num_of_var_parameters+num_of_var_compartments+i], var_spr[i]->prev_k[0], var_spr[i]->prev_k[1], dt);
            //numerical differentiation
            jacobian[num_of_var_species+num_of_var_parameters+num_of_var_compartments+i][loop] = (delta_value[num_of_var_species+num_of_var_parameters+num_of_var_compartments+i]-b[num_of_var_species+num_of_var_parameters+num_of_var_compartments+i])/delta;
          }
          if(loop < num_of_var_species){
            var_sp[loop]->temp_value -= delta;
          }else if(loop < num_of_var_species+num_of_var_parameters){
            var_param[loop-num_of_var_species]->temp_value -= delta;
          }else if(loop < num_of_var_species+num_of_var_parameters+num_of_var_compartments){
            var_comp[loop-num_of_var_species-num_of_var_parameters]->temp_value -= delta;
          }else{
            var_spr[loop-num_of_var_species-num_of_var_parameters-num_of_var_compartments]->temp_value -= delta;
          }
        }
      }

      //initialize p
      for(i=0; i<num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference; i++){
        p[i] = i;
      }

      //LU decomposition
      error = lu_decomposition(jacobian, p, num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference);
      if(error == 0){//failure in LU decomposition
        return NULL;
      }

      //forward substitution & backward substitution
      lu_solve(jacobian, p, num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference, b);

      //calculate next temp value
      for(i=0; i<num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference; i++){
        if(i < num_of_var_species){
          var_sp[i]->temp_value -= b[i];
        }else if(i < num_of_var_species+num_of_var_parameters){
          var_param[i-num_of_var_species]->temp_value -= b[i];
        }else if(i < num_of_var_species+num_of_var_parameters+num_of_var_compartments){
          var_comp[i-num_of_var_species-num_of_var_parameters]->temp_value -= b[i];
        }else{
          var_spr[i-num_of_var_species-num_of_var_parameters-num_of_var_compartments]->temp_value -= b[i];
        }
      }

      //convergence judgement
      if(use_lazy_method){
        is_convergence = 1;
        for(i=0; i<num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference; i++){
          if(fabs(b[i]) > fabs(pre_b[i])){
            is_convergence = 0;
          }
        }
        for(i=0; i<num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference; i++){
          pre_b[i] = b[i];
        }
      }

      //error judgement
      flag = 0;
      for(i=0; i<num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference; i++){
        if(fabs(b[i]) > tolerance){
          flag = 1;
        }
      }
    }

    //calc temp value by assignment
    for(i=0; i<num_of_all_var_species; i++){
      if(all_var_sp[i]->depending_rule != NULL && all_var_sp[i]->depending_rule->is_assignment){
        all_var_sp[i]->temp_value = calc(all_var_sp[i]->depending_rule->eq, dt, cycle, &reverse_time, 0);
      }
    }
    for(i=0; i<num_of_all_var_parameters; i++){
      if(all_var_param[i]->depending_rule != NULL && all_var_param[i]->depending_rule->is_assignment){
        all_var_param[i]->temp_value = calc(all_var_param[i]->depending_rule->eq, dt, cycle, &reverse_time, 0);
      }
    }
    for(i=0; i<num_of_all_var_compartments; i++){
      if(all_var_comp[i]->depending_rule != NULL && all_var_comp[i]->depending_rule->is_assignment){
        all_var_comp[i]->temp_value = calc(all_var_comp[i]->depending_rule->eq, dt, cycle, &reverse_time, 0);
      }
    }
    for(i=0; i<num_of_all_var_species_reference; i++){
      if(all_var_spr[i]->depending_rule != NULL && all_var_spr[i]->depending_rule->is_assignment){
        all_var_spr[i]->temp_value = calc(all_var_spr[i]->depending_rule->eq, dt, cycle, &reverse_time, 0);
      }
    }

    //calc independent species value
    for(i=0; i<num_of_species; i++){
      if(sp[i]->is_concentration && sp[i]->is_independent
          && !sp[i]->depending_event_is_fired){
        sp[i]->temp_value = sp[i]->initial_amount/sp[i]->locating_compartment->temp_value;
        sp[i]->value = sp[i]->temp_value;
      }
    }     

    //calc temp value algebraic by algebraic
    if(algEq != NULL){
      if(algEq->num_of_algebraic_variables > 1){
        //initialize pivot
        for(i=0; i<algEq->num_of_algebraic_variables; i++){
          alg_pivot[i] = i;
        }
        for(i=0; i<algEq->num_of_algebraic_variables; i++){
          for(j=0; j<algEq->num_of_algebraic_variables; j++){
            coefficient_matrix[i][j] = calc(algEq->coefficient_matrix[i][j], dt, cycle, &reverse_time, 0);
          }
        }
        for(i=0; i<algEq->num_of_algebraic_variables; i++){
          constant_vector[i] = -calc(algEq->constant_vector[i], dt, cycle, &reverse_time, 0);
        }
        //LU decompostion
        error = lu_decomposition(coefficient_matrix, alg_pivot, algEq->num_of_algebraic_variables);
        if(error == 0){//failure in LU decomposition
          return NULL;
        }
        //forward substitution & backward substitution
        lu_solve(coefficient_matrix, alg_pivot, algEq->num_of_algebraic_variables, constant_vector);
        for(i=0; i<algEq->num_of_alg_target_sp; i++){
          algEq->alg_target_species[i]->target_species->temp_value = constant_vector[algEq->alg_target_species[i]->order];
        }    
        for(i=0; i<algEq->num_of_alg_target_param; i++){
          algEq->alg_target_parameter[i]->target_parameter->temp_value = constant_vector[algEq->alg_target_parameter[i]->order];
        }    
        for(i=0; i<algEq->num_of_alg_target_comp; i++){
          algEq->alg_target_compartment[i]->target_compartment->temp_value = constant_vector[algEq->alg_target_compartment[i]->order];
        }    
      }else{
        if(algEq->target_species != NULL){
          algEq->target_species->temp_value = -calc(algEq->constant, dt, cycle, &reverse_time, 0)/calc(algEq->coefficient, dt, cycle, &reverse_time, 0);
        }
        if(algEq->target_parameter != NULL){
          algEq->target_parameter->temp_value = -calc(algEq->constant, dt, cycle, &reverse_time, 0)/calc(algEq->coefficient, dt, cycle, &reverse_time, 0);
        }
        if(algEq->target_compartment != NULL){
          algEq->target_compartment->temp_value = -calc(algEq->constant, dt, cycle, &reverse_time, 0)/calc(algEq->coefficient, dt, cycle, &reverse_time, 0);
        }
      }
    }

    //preserve prev_value and prev_k for multistep solution
    for(i=0; i<num_of_var_species; i++){
      var_sp[i]->prev_val[2] = var_sp[i]->prev_val[1];
      var_sp[i]->prev_val[1] = var_sp[i]->prev_val[0];
      var_sp[i]->prev_val[0] = var_sp[i]->value;
      var_sp[i]->prev_k[2] = var_sp[i]->prev_k[1];
      var_sp[i]->prev_k[1] = var_sp[i]->prev_k[0];
      var_sp[i]->prev_k[0] = k_t[i];
    }
    for(i=0; i<num_of_var_parameters; i++){
      var_param[i]->prev_val[2] = var_param[i]->prev_val[1];
      var_param[i]->prev_val[1] = var_param[i]->prev_val[0];
      var_param[i]->prev_val[0] = var_param[i]->value;
      var_param[i]->prev_k[2] = var_param[i]->prev_k[1];
      var_param[i]->prev_k[1] = var_param[i]->prev_k[0];
      var_param[i]->prev_k[0] = k_t[num_of_var_species+i];
    }
    for(i=0; i<num_of_var_compartments; i++){
      var_comp[i]->prev_val[2] = var_comp[i]->prev_val[1];
      var_comp[i]->prev_val[1] = var_comp[i]->prev_val[0];
      var_comp[i]->prev_val[0] = var_comp[i]->value;
      var_comp[i]->prev_k[2] = var_comp[i]->prev_k[1];
      var_comp[i]->prev_k[1] = var_comp[i]->prev_k[0];
      var_comp[i]->prev_k[0] = k_t[num_of_var_species+num_of_var_parameters+i];
    }
    for(i=0; i<num_of_var_species_reference; i++){
      var_spr[i]->prev_val[2] = var_spr[i]->prev_val[1];
      var_spr[i]->prev_val[1] = var_spr[i]->prev_val[0];
      var_spr[i]->prev_val[0] = var_spr[i]->value;
      var_spr[i]->prev_k[2] = var_spr[i]->prev_k[1];
      var_spr[i]->prev_k[1] = var_spr[i]->prev_k[0];
      var_spr[i]->prev_k[0] = k_t[num_of_var_species+num_of_var_parameters+i];
    }

    //forwarding value
    forwarding_value(all_var_sp, num_of_all_var_species, all_var_param, num_of_all_var_parameters, all_var_comp, num_of_all_var_compartments, all_var_spr, num_of_all_var_species_reference);
  }
  printf("Simulation Ends!\n");
  fclose(fp1);
  fclose(fp2);
  fclose(fp3);
  if(algEq != NULL){
    for(i=0; i<algEq->num_of_algebraic_variables; i++){
      free(coefficient_matrix[i]);
    }
    free(coefficient_matrix);
    free(constant_vector);
    free(alg_pivot);
  }
  for(i=0; i<num_of_var_species+num_of_var_parameters+num_of_var_compartments+num_of_var_species_reference; i++){
    free(jacobian[i]);
  }
  free(jacobian);
  return result;
}
