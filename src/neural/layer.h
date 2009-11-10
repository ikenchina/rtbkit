/* layer.h                                                         -*- C++ -*-
   Jeremy Barnes, 20 October 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Layers for perceptrons and other similar beasts.
*/

#ifndef __jml__layer_h__
#define __jml__layer_h__

#include "perceptron_defs.h"
#include "boosting/thread_context.h"
#include "stats/distribution.h"
#include <boost/multi_array.hpp>
#include "parameters.h"


namespace ML {


/*****************************************************************************/
/* LAYER                                                                     */
/*****************************************************************************/

/** A basic layer of a neural network.  Other kinds of layers can be built on
    this base.
*/

class Layer {

protected:
    /** Constructor to be called from a subclass.  Initializes the standard
        members. */
    Layer(const std::string & name,
          size_t inputs, size_t outputs);
    
    Layer(const Layer & other);

    Layer & operator = (const Layer & other);
    
    void init(const std::string & name, size_t inputs, size_t outputs);

    void swap(Layer & other);

    bool operator == (const Layer & other) const;

public:
    
    /*************************************************************************/
    /* INFO                                                                  */
    /*************************************************************************/

    /** \name Information
        These functions provide information about the layer.
        
        @{
    */

    /** Dump as ASCII.  This will be big. */
    virtual std::string print() const = 0;
    
    /** Return the name of the type */
    virtual std::string class_id() const = 0;

    size_t inputs() const { return inputs_; }
    size_t outputs() const { return outputs_; }

    virtual size_t max_width() const { return std::max(inputs_, outputs_); }

    std::string name() const { return name_; }

    /** Given the activation function and the maximum amount of the range
        that we want to use (eg, 0.8 for asymptotic functions), what are
        the minimum and maximum values that we want to use.

        For example, tanh goes from -1 to 1, but asymptotically.  We would
        normally want to go from -0.8 to 0.8, so that we didn't force too
        hard to get there.
    */
    virtual std::pair<float, float> targets(float maximum) const = 0;

    /** Check that all parameters are reasonable and invariants are met.
        Will throw an exception if there is a problem. */
    virtual void validate() const;

    /** Check if the two are equal (replacing one by the other would have no
        effect).  This function checks that the types of the two objects are
        the same and then calls equal_impl(). */
    bool equal(const Layer & other) const;

    virtual bool equal_impl(const Layer & other) const = 0;

    ///@}


    /*************************************************************************/
    /* PARAMETERS                                                            */
    /*************************************************************************/

    /** \name Parameters

        Provides information about parameters of the layer.

        @{
    */

    /** Return a reference to a parameters object that describes this layer's
        parameters.  It should provide a reference. */
    Parameters_Ref & parameters() { return parameters_; }
    const Parameters_Ref & parameters() const { return parameters_; }

    /** Update the parameters.  This should be called on the object whenever
        anything happens that means that the parameters change at all (even
        if they are just freed and re-allocated).

        It will clear the current parameters and call add_parameters() on
        them.
    */
    void update_parameters();

    /** Add all of our parameters to the given parameters object. */
    virtual void add_parameters(Parameters & params) = 0;

    /** Return the number of parameters (degrees of freedom) for the
        layer. */
    virtual size_t parameter_count() const = 0;

    /** Fill with random weights. */
    virtual void random_fill(float limit, Thread_Context & context) = 0;

    virtual void zero_fill() = 0;

    ///@}
    
    /*************************************************************************/
    /* SERIALIZATION                                                         */
    /*************************************************************************/

    /** \name Serialization
        
        These functions are associated with serializing and reconstituting
        the object into a binary format, as well as manipulating them
        polymorphically.
        
        @{
    */

    /** Serialize the type-specific internal data into the store, but no
        information about the type itself. */
    virtual void serialize(DB::Store_Writer & store) const = 0;

    /** Reconstitute the type-specific internal data from the store. */
    virtual void reconstitute(DB::Store_Reader & store) = 0;

    /** Make a copy of the object.  If it is a layer that references other
        objects (such as through shared pointers), then the references will
        also be held by the new object.  Use deep_copy() if this is not
        desired.
    */
    virtual Layer * make_copy() const = 0;

    /** Make a copy that is not connected to those underneath. */
    virtual Layer * deep_copy() const = 0;

    /** Serialize the object to the given store, as well as its type
        information.  The object thus serialized can be reconstituted with
        poly_reconstitute(); the reconstitute() method will fail as it
        does not expect the type information to be there. */
    void poly_serialize(ML::DB::Store_Writer & store) const;

    /** Reconstitute an object from the given store, returning a shared
        pointer to it.  This is the counterpart to poly_serialize(); calling
        this method on a store where an object was only serialize()d will
        fail. */
    static boost::shared_ptr<Layer>
    poly_reconstitute(ML::DB::Store_Reader & store);

    // @)


    /*************************************************************************/
    /* APPLY                                                                 */
    /*************************************************************************/

    /** \name Apply

        These functions take an input and return the output.  Note that,
        although they perform the same function as a fprop, they don't
        attempt to save information that is necessary for the bprop later, and
        so are more efficient.

        @{
    */

    /** Apply the layer to the input and return an output. */
    distribution<float> apply(const distribution<float> & input) const;
    distribution<double> apply(const distribution<double> & input) const;
        
    void apply(const distribution<float> & input,
               distribution<float> & output) const;

    void apply(const distribution<double> & input,
               distribution<double> & output) const;

    /** Apply the layer to the input and put the result in the output
        array.

        \param input   Array (of size inputs()) of input values
        \param output  Array (of size outputs()) of output values

        <b>NOTE</b> that input and output <b>may be the same array</b>.  The
        routine should function correctly even if this is the case, by
        copying input into a temporary buffer before using it if it overlaps
        with output and this affects the calculation. */
    virtual void apply(const float * input, float * output) const = 0;

    /** \copydoc apply */
    virtual void apply(const double * input, double * output) const = 0;

    ///@}


    /*************************************************************************/
    /* FPROP                                                                 */
    /*************************************************************************/

    /** \name Forward Propagation

        These methods are used to implement the forward propagation pass of
        the training.  The forward propagation is very similar to the apply()
        function: given the inputs, it will calculate the outputs of the
        current layer.  However, unlike apply(), these functions can store
        information that is useful to the backpropagation.

        The main difference with apply() is that temporary space is available.
        This temporary space serves two functions: it records the outputs of
        previous layers (when propagating through multiple layers) and it
        allows intermediate results to be kept (for example, some transfer
        functions are much faster to differentiate if their activation
        values are stored).

        Looking at the specifics of the input to the function, we have:

        <pre>
               +---------+----------------+-------------+
               | inputs  |  temp space    | outputs     |
               +---------+----------------+-------------+
               ^         ^                ^             ^
               |         |                |             |
               t - i     t                t + s         t + s + o
        </pre>
        
        where t is the temp_space pointer, s is the temp space size requested
        in fprop_temporary_space_size(), i is the number of inputs and o is
        the number of outputs.  (Note that all sizes are given in elements
        (float or double), not in bytes).

        The goal of the fprop function is to read the inputs (which are
        already filled in) and write the outputs to the correct place, whilst
        filling in the temp space.

        For functions that don't need anything but the inputs and outputs to
        be stored (this is true for most of them), they can return a
        fprop_temporary_space_required() of zero.

        @{
    */

    /** Return the amount of space necessary to save temporary results for the
        forward prop.  There will be an array of the given precision (double
        or single) provided.

        The default implementation (if not overridden) returns 0.
    */
    virtual size_t fprop_temporary_space_required() const = 0;

    /** These functions perform a forward propagation.  They also save whatever
        information is necessary to perform an efficient backprop at a later
        period in time.

        Default implementation calls apply() and assumes a temporary space
        size of zero.

        \param inputs      Pointer to the start of an array of inputs()
                           elements providing the input values.

        \param temp_space  Pointer to the start of an array of
                           temp_space_size uninitialized
                           elements providing temporary space to store the
                           information necessary to perform a bprop() later.

        \param temp_space_size  The size of the temp_space array, which
                           matches the output of the
                           fprop_temporary_space_required() function.

        \param outputs     Pointer to the start of an array of outputs()
                           uninitialized elements in which the output values
                           will be stored.
    */
    virtual void
    fprop(const float * inputs,
          float * temp_space, size_t temp_space_size,
          float * outputs) const = 0;

    /** \copydoc fprop */
    virtual void
    fprop(const double * inputs,
          double * temp_space, size_t temp_space_size,
          double * outputs) const = 0;

    ///@}


    /*************************************************************************/
    /* BPROP                                                                 */
    /*************************************************************************/

    /** \name Backward Propagation

        These functions calculate the gradient of an error function with
        respect to each parameter, in order to perform gradient descent.

        @{
    */

    /** Perform a back propagation.  Given the derivative of the error with
        respect to each of the errors, they compute the gradient of the
        parameter space.

        \param inputs     An array of inputs() elements with the inputs to this
                          layer when the fprop() was performed.
        \param outputs    An array of outputs() elements with the outputs of
                          this layer as calculated by fprop().
        \param temp_space An array of temp_space_size elements that was filled
                          in by fprop() with any extra information necessary to
                          perform the bprop().
        \param temp_space_size The number of elements in temp_space(), which
                          should match fprop_temporary_space_required().
        \param output_errors An array of outputs() elements with the derivative
                          of the error function with respect to each of the
                          outputs of this layer.  These are the errors to be
                          backpropagated through.
        \param input_errors An array of inputs() elements.  The derivative of
                          the error function with respect to each of the
                          inputs to the layer should be calculated and put
                          into this array.  <b>NOTE</b> that this array could be
                          null, in which case no input errors should be
                          calculated.
        \param gradient   The parameters array to be updated.  Each parameter
                          should have example_weight * dE/dparam added to it,
                          where dE/dparam is the derivative of the error with
                          respect to each parameter.
        \param example_weight The weight of this example.  The dE/dparam
                          value will be multiplied by this value before it
                          is added to the gradient.

        <b>NOTE</b>: this function should be able to work where input_errors and
        output_errors point to the same range of memory.  If the calculation
        of input_errors uses output_errors, then a copy of input_errors needs
        to be made so that they are available during this calculation.
    */

    virtual void bprop(const float * inputs,
                       const float * outputs,
                       const float * temp_space, size_t temp_space_size,
                       const float * output_errors,
                       float * input_errors,
                       Parameters & gradient,
                       double example_weight) const = 0;

    /** \copydoc bprop */
    virtual void bprop(const double * inputs,
                       const double * outputs,
                       const double * temp_space, size_t temp_space_size,
                       const double * output_errors,
                       double * input_errors,
                       Parameters & gradient,
                       double example_weight) const = 0;
    ///@}

#if 0
    /*************************************************************************/
    /* BBPROP                                                                */
    /*************************************************************************/

    /** Second derivative of the parameters with respect to the errors.  Used
        to determine an individual learning rate for each of the parameters.
    */

    /** Does this implement bbprop? */
    virtual bool has_bbprop() const = 0;
 
    /* (todo) */
#endif


protected:
    std::string name_;
    size_t inputs_, outputs_;

    /** Contains a reference to our parameters. */
    Parameters_Ref parameters_;
};

inline std::ostream & operator << (std::ostream & stream, const Layer & layer)
{
    return stream << layer.print();
}

} // namespace ML

#endif /* __jml__layer_h__ */
